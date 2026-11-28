#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSRPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/General/IndirectLightingPolicy.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"

namespace
{
	// 与 SSReflect.comp 的 CubeParams 对应（std140 bool = 4 字节）
	struct CubeParams {
		uint32_t CubeEnable = 0;
		uint32_t IBLEnable = 0;
		uint32_t HitDiffuseMode = 0;	// 命中点漫反射间接光：0 = 场景颜色里已有，1 = IBL，2 = DDGI（没覆盖到的回退 IBL）
	};

	struct SSRParams
	{
		float tMin;
		float tMax;
		uint32_t maxBounce = 0;
		uint32_t SampleRayCount;
		uint32_t RayMarchingMaxStep;
		float DistanceFactor;
		uint32_t frameIndex;
	};

	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kDDGIFrameKey = "ssrDDGIFrame";
}


SSRPass::SSRPass(const std::string& computerShaderPath)
	:
	_firstDrawTemporal(true),
	_enable(false)
{

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.AddDefineMacro("Max_Bounce_limit", GlobalConfig::SSTrace_Max_Bounce_limit);
		config.computePath = computerShaderPath;
		DDGIAddLayoutDefines(config);		// 命中点采样 DDGI，图集布局宏与 DDGI Pass 一致

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddUnifromBuffer(1)
			.AddStorageImage(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4)
			.AddUnifromTexture(5)
			.AddUnifromTexture(6)
			.AddUnifromTexture(7)
			.AddUnifromTexture(8)
			.AddUnifromTexture(9)
			.AddUnifromTexture(10)
			.AddUnifromTexture(11)
			.AddUnifromTexture(12)
			.AddUnifromTexture(13)
			.AddUnifromTexture(14)
			.AddUnifromBuffer(15)		// DDGIUniforms
			.AddStorageBuffer(16)		// DDGI 体积表
			.AddUnifromTexture(17)		// DDGI 辐照度图集
			.AddUnifromTexture(18)		// DDGI 深度图集
			.AddUnifromTexture(19)		// DDGI probeData
			.AddUnifromBuffer(20);		// 反射光线上的高度雾（ReflectionFog.comp，和主画面同一份参数）

		if (config.Validate())
			_ssrShader.Create(config);
	}


	_SSRParamsUBO = std::make_shared<UniformBlock>(sizeof(SSRParams));
}

bool SSRPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssrTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSRPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	FrameRenderData data;

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.atlasShadowMap = ctx.GetInput(4);
	data.ssaoMap = ctx.GetInput(5);
	data.gMotionVector = ctx.GetInput(6);
	data.hzbDepthMap = ctx.GetInput(7);

	// DDGI 图集（可选持久资源：DDGI 从没执行过时为空；是否使用由下面的 ddgiEnable 决定）
	data.ddgiIrradianceAtlas = ctx.GetOptionalPersitent(0);
	data.ddgiDepthAtlas = ctx.GetOptionalPersitent(1);
	data.ddgiProbeData = ctx.GetOptionalPersitent(2);

	data.gDepthStencil = ctx.GetFrameLocal(0);

	data.originTexture = ctx.GetTemp(0);
	data.temporalAccumulateColorTexture = ctx.GetTemp(1);
	data.temporalAccumulateMomentTexture = ctx.GetTemp(2);
	data.spatialDenoisingTempTexture = ctx.GetTemp(3);
	data.denoisedTexture = ctx.GetTemp(4);

	data.temporalAccumulateHistoryColorTexture = ctx.GetPersitent(0);
	data.temporalAccumulateHistoryMomentTexture = ctx.GetPersitent(1);
	data.gPrevPosition = ctx.GetPersitent(2);
	data.gPrevNormal = ctx.GetPersitent(3);
	data.gPrevDepthStencil = ctx.GetPersitent(4);

	data.outPutTexture = ctx.GetOutput(0);

	data.sceneColorMap = ctx.GetExternal(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawSSR(cmd, data, registry, state)) return;

	if (!DrawTemporalAccumulate(cmd, data, state)) return;

	if (data.temporalAccumulateColorTexture && data.temporalAccumulateHistoryColorTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateColorTexture, data.temporalAccumulateHistoryColorTexture);
	if (data.temporalAccumulateMomentTexture && data.temporalAccumulateHistoryMomentTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateMomentTexture, data.temporalAccumulateHistoryMomentTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (!DrawSpatialDenoising(cmd, data, state)) return;

	// 降噪调试：用时域累积的调试热力图覆盖输出（不重新调制），最终合成时替换场景颜色显示
	if (auto debugTexture = _temporalAccumulate.GetDebugTexture(); debugTexture && data.outPutTexture)
	{
		Texture2D::CopyTextureAsync(cmd, debugTexture, data.outPutTexture);
		if (cmd->IsRecording())
			cmd->SubmitToQueue();
		return;
	}

	DrawRemodulate(cmd, data, state);
}

void SSRPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.ssrOn);

	registry.Store(kDDGIFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;

	// 命中点的漫反射间接光取自 DDGI：与 DDGI 各 Pass 共用同一帧的解析结果（Resolve 按帧缓存、内部加锁）
	if (_sharedState && GetIndirectLightingPolicy(state).ddgiOn)
		registry.Store(kDDGIFrameKey, _sharedState->Resolve(state));


	auto& binding = registry.Get<ComputeBindingRecord>("binding");

	{
		SSRParams params{
			.tMin = std::max(0.f, state.option.ssrTraceParams.tMin),
			.tMax = std::max(0.f, state.option.ssrTraceParams.tMax),
			.maxBounce = std::max(1u, std::min(state.option.ssrTraceParams.maxBounceLimit, GlobalConfig::SSTrace_Max_Bounce_limit)),
			.SampleRayCount = state.option.ssrTraceParams.NumSamples,
			.RayMarchingMaxStep = std::max(2u, state.option.ssrTraceParams.RayMarchingMaxStep),
			.DistanceFactor = std::max(0.0001f, state.option.ssrTraceParams.DistanceFactor),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_SSRParamsUBO->WriteData(&params, sizeof(SSRParams));
		binding.SetUniformBlock(_SSRParamsUBO, 0);
	}

}

void SSRPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}

void SSRPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool SSRPass::DrawSSR(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;

	auto size = target->GetSize();
	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	auto& binding = registry.Get<ComputeBindingRecord>("binding");

	CubeParams cubeParams;
	const IBLResources ibl = SelectIBLResources(state);

	// cubeMap（天空反射）优先用 IBL 产出的最终天空 cube（含云），和 IBL 一致；
	// 没有 IBL 时回退到原始大气 / 天空盒 cube，保证关闭云与旧行为一致。
	std::shared_ptr<TextureCube> cubeMap;
	if (ibl.skyCube && !ibl.skyCube->IsEmpty())
	{
		cubeParams.CubeEnable = true;
		cubeMap = ibl.skyCube;
	}
	else if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
	{
		if (state.skyAtmosphereParams.skyCube && !state.skyAtmosphereParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyAtmosphereParams.skyCube;
		}
	}
	else if (state.option.flags.skyboxOn)
	{
		if (state.skyboxParams.skyCube && !state.skyboxParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyboxParams.skyCube;
		}
	}

	std::shared_ptr<TextureCube> IBLDiffuse;
	std::shared_ptr<TextureCube> IBLPrefilter;
	std::shared_ptr<Texture2D> IBLBrdfLUT;
	if (ibl.IBLReady())
	{
		cubeParams.IBLEnable = true;
		IBLDiffuse = ibl.diffuse;
		IBLPrefilter = ibl.prefilter;
		IBLBrdfLUT = ibl.brdfLUT;
	}

	// 命中点漫反射间接光（与 RTR 相同，按命中点的世界位置 / 法线采样，不读屏幕空间的间接光合成结果）：
	//   LightingPass 已经把 IBL 漫反射加进场景颜色 → 0（不重复加）
	//   DDGI 开启、有可用体积、图集本帧已由 ddgiBlendNode 写好 → 2（DDGI，没覆盖到的回退 IBL）
	//   其余（DDGI 没开或没体积、路径追踪开启时 LightingPass 不补漫反射）→ 1（IBL）
	std::shared_ptr<const DDGIFrame> ddgiFrame;
	if (auto* framePtr = registry.TryLoad<DDGIFramePtr>(kDDGIFrameKey))
		ddgiFrame = *framePtr;
	registry.Store(kDDGIFrameKey, DDGIFramePtr{});
	auto texReady = [](const std::shared_ptr<Texture2D>& tex) { return tex && !tex->IsEmpty(); };
	const bool ddgiEnable = ddgiFrame && ddgiFrame->enabled && ddgiFrame->uniformBlock && ddgiFrame->volumeTable
		&& texReady(data.ddgiIrradianceAtlas) && texReady(data.ddgiDepthAtlas) && texReady(data.ddgiProbeData);
	const IndirectLightingPolicy policy = GetIndirectLightingPolicy(state);
	const bool sceneColorHasIBLDiffuse = policy.lightingIBLDiffuse && SelectIBLResources(state).IBLReady();
	cubeParams.HitDiffuseMode = sceneColorHasIBLDiffuse ? 0u : (ddgiEnable ? 2u : 1u);

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(cubeParams));
	paramsUBO->WriteDataAsync(cmd, &cubeParams, sizeof(cubeParams));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);


	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetUniformBlock(paramsUBO, 1);
	binding.SetUniformBlock(CreateReflectionFogUniformBlock(cmd, state, state.option.ssrTraceParams.fogVolumeMode), 20);
	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(data.sceneColorMap, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 8);
	binding.SetUniformTexture(data.ssaoMap, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(data.hzbDepthMap, vk::ImageAspectFlagBits::eDepth, 10);

	if (cubeParams.CubeEnable)
		binding.SetUniformTextureCube(cubeMap, vk::ImageAspectFlagBits::eColor, 11);

	if (cubeParams.IBLEnable)
	{
		binding.SetUniformTextureCube(IBLDiffuse, vk::ImageAspectFlagBits::eColor, 12);
		binding.SetUniformTextureCube(IBLPrefilter, vk::ImageAspectFlagBits::eColor, 13);
		binding.SetUniformTexture(IBLBrdfLUT, vk::ImageAspectFlagBits::eColor, 14);
	}

	if (cubeParams.HitDiffuseMode == 2u)
	{
		binding.SetUniformBlock(ddgiFrame->uniformBlock, 15);
		binding.SetStorageBlock(ddgiFrame->volumeTable, 16);
		binding.SetUniformTexture(data.ddgiIrradianceAtlas, vk::ImageAspectFlagBits::eColor, 17);
		binding.SetUniformTexture(data.ddgiDepthAtlas, vk::ImageAspectFlagBits::eColor, 18);
		binding.SetUniformTexture(data.ddgiProbeData, vk::ImageAspectFlagBits::eColor, 19);
	}

	_ssrShader.Bind(cmd, binding);
	cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSRPass::DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.originTexture;
	auto& target = data.temporalAccumulateColorTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source
		|| source->IsEmpty()
		|| !target
		|| target->IsEmpty()
		|| !moment
		|| moment->IsEmpty()
		)
		return false;

	if (_firstDrawTemporal)
	{
		vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		vk::ClearColorValue clearMoments = { 0.0f, 0.0f, 0.0f, 0.0f };
		vk::ImageSubresourceRange range;
		range.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
			.setBaseMipLevel(0)
			.setLevelCount(1);
		cmd->clearColorImage(data.temporalAccumulateHistoryColorTexture->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
		cmd->clearColorImage(data.temporalAccumulateHistoryMomentTexture->GetImage(), vk::ImageLayout::eGeneral, clearMoments, range);
		_firstDrawTemporal = false;
	}

	TemporalAccumulate::Params params{
		.maxAccumulateCount = state.option.ssrTraceParams.maxAccumulateCount,
		.depthThreshold = state.option.ssrTraceParams.depthThreshold,
		.positiondistThreshold = state.option.ssrTraceParams.positiondistThreshold,
		.normalThreshold = state.option.ssrTraceParams.normalThreshold,
		.motionWeightScale = state.option.ssrTraceParams.motionWeightScale,
		.varianceClipGamma = state.option.ssrTraceParams.varianceClipGamma,
		.debugMode = state.option.denoiseDebug.ModeFor(Scene3DRenderOption::DenoiseDebugTarget::SSR),
		// 镜面信号：SSReflect.comp 把第一次反弹的命中距离写在 alpha 里，时域累积额外做虚像重投影
		.signalType = static_cast<uint32_t>(TemporalAccumulate::SignalType::Specular)
	};

	_temporalAccumulate.Execute(
		cmd,
		source,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gMotionVector,
		data.gPrevPosition,
		data.gPrevNormal,
		data.gPrevDepthStencil,
		target,
		moment,
		data.temporalAccumulateHistoryColorTexture,
		data.temporalAccumulateHistoryMomentTexture,
		params,
		state.camera.curUBO,
		state.camera.prevUBO,
		data.gMetallicRoughness
	);

	cmd->SubmitToQueue();

	return true;
}

bool SSRPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.temporalAccumulateColorTexture;
	auto& target = data.denoisedTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	AtrousBilateralFilter::Params params{
		.normalFactor = state.option.ssrTraceParams.normalFactor,
		.depthFactor = state.option.ssrTraceParams.depthFactor,
		.luminanceFactor = state.option.ssrTraceParams.luminanceFactor,
		.specularMode = 1
	};

	_spatialDenoisingFilter.Execute(
		cmd,
		source,
		data.gNormal,
		data.gDepthStencil,
		moment,
		target,
		data.spatialDenoisingTempTexture,
		params,
		state.camera.curUBO,
		state.camera.prevUBO,
		state.option.ssrTraceParams.filterCount,
		data.gMetallicRoughness		// 镜面信号：按粗糙度 / 命中距离限制滤波半径
	);

	cmd->SubmitToQueue();

	return true;
}

bool SSRPass::DrawRemodulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	// SSReflect.comp 已除以镜面反射率（与 RTR 一致），这里乘回（同分辨率，联合双边上采样退化为逐像素）
	if (!data.denoisedTexture || data.denoisedTexture->IsEmpty() || !data.outPutTexture || data.outPutTexture->IsEmpty())
		return false;

	_remodulate.Execute(
		cmd,
		data.denoisedTexture,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gAlbedoOpacity,
		data.gMetallicRoughness,
		data.outPutTexture,
		IndirectUpsample::Remodulation::Specular,
		state.camera.curUBO,
		state.camera.prevUBO
	);

	cmd->SubmitToQueue();

	return true;
}

void SSRPass::OnResize(uint32_t width, uint32_t height)
{
	// 渲染图按新尺寸重建，历史纹理是新分配的（内容未定义）：下一帧先清历史
	_firstDrawTemporal = true;
}
