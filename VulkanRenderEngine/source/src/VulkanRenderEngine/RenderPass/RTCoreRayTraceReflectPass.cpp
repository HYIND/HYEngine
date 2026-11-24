#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceReflectPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/General/IndirectLightingPolicy.h"

namespace
{
	// 与 RayTraceReflect.rgen 的 CubeParams 对应（std140 bool = 4 字节）
	struct CubeParams {
		uint32_t CubeEnable = 0;
		uint32_t IBLEnable = 0;
		uint32_t DDGIEnable = 0;	// 命中点的漫反射间接光取自 DDGI
	};

	struct RayTraceParams
	{
		float tMin = 0.f;
		float tMax = 0.f;
		uint32_t maxBounce = 0;
		uint32_t sampleRayCount = 0;
		uint32_t frameIndex;
	};

	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kDDGIFrameKey = "rtReflectDDGIFrame";
}


RTCoreRayTraceReflectPass::RTCoreRayTraceReflectPass(
	const std::string& raygenPath,
	const std::string& missPath,
	const std::string& closestHitPath,
	const std::string& anyHitPath,
	const std::string& intersectionPath,
	const std::string& callablePath
)
	:
	_firstDrawTemporal(true),
	_enable(false)
{

	{
		RayTracingPipelineConfig config;
		config.raygenPath = raygenPath;
		config.missPath = missPath;
		config.extraMissPaths = { "shader/RTCoreRayTrace/ShadowMiss.rmiss" };	// miss 1：阴影光线（RayTraceGeneral.comp 的 TraceShadowRay）
		config.closestHitPath = closestHitPath;
		config.anyHitPath = anyHitPath;
		config.intersectionPath = intersectionPath;
		config.callablePath = callablePath;
		DDGIAddLayoutDefines(config);		// 命中点采样 DDGI，图集布局宏与 DDGI Pass 一致

		config
			.AddCameraUnifromDataBinding()
			.AddLightDataBinding()
			.AddBindlessMaterialTextureBinding()
			.AddAccelerationStructure(0)
			.AddStorageBuffer(1)
			.AddStorageBuffer(2)
			.AddStorageBuffer(3)
			.AddStorageBuffer(4)
			.AddUnifromBuffer(5)
			.AddUnifromBuffer(6)
			.AddStorageImage(7)
			.AddUnifromTexture(8)
			.AddUnifromTexture(9)
			.AddUnifromTexture(10)
			.AddUnifromTexture(11)
			.AddUnifromTexture(12)
			.AddUnifromTexture(13)
			.AddUnifromTexture(14)
			.AddUnifromTexture(15)
			.AddUnifromTexture(16)
			.AddUnifromTexture(17)
			.AddUnifromTexture(18)
			.AddUnifromBuffer(19)		// DDGIUniforms
			.AddStorageBuffer(20)		// DDGI 体积表
			.AddUnifromTexture(21)		// DDGI 辐照度图集
			.AddUnifromTexture(22)		// DDGI 深度图集
			.AddUnifromTexture(23)		// DDGI probeData
			.AddStorageImage(24);		// 第一次反弹命中点的运动（时域累积的虚像重投影）

		if (config.Validate())
			_rayTraceShader.Create(config);
	}

	_RayTraceParamsUBO = std::make_shared<UniformBlock>(sizeof(RayTraceParams));
}

RTCoreRayTraceReflectPass::~RTCoreRayTraceReflectPass()
{}

bool RTCoreRayTraceReflectPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.rayTraceReflectParams.maxBounceLimit < 0)
		return false;
	return true;
}

void RTCoreRayTraceReflectPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
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

	data.gDepthStencil = ctx.GetFrameLocal(0);

	data.originTexture = ctx.GetTemp(0);
	data.temporalAccumulateColorTexture = ctx.GetTemp(1);
	data.temporalAccumulateMomentTexture = ctx.GetTemp(2);
	data.spatialDenoisingTempTexture = ctx.GetTemp(3);
	data.denoisedTexture = ctx.GetTemp(4);
	data.hitMotionTexture = ctx.GetTemp(5);

	data.temporalAccumulateHistoryColorTexture = ctx.GetPersitent(0);
	data.temporalAccumulateHistoryMomentTexture = ctx.GetPersitent(1);
	data.gPrevPosition = ctx.GetPersitent(2);
	data.gPrevNormal = ctx.GetPersitent(3);
	data.gPrevDepthStencil = ctx.GetPersitent(4);

	data.outPutTexture = ctx.GetOutput(0);

	// DDGI 图集（可选持久资源：DDGI 从没执行过时为空；是否使用由下面的 ddgiEnable 决定）
	data.ddgiIrradianceAtlas = ctx.GetOptionalPersitent(0);
	data.ddgiDepthAtlas = ctx.GetOptionalPersitent(1);
	data.ddgiProbeData = ctx.GetOptionalPersitent(2);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawRayTraceReflect(cmd, data, registry, state)) return;

	if (!DrawTemporalAccumulate(cmd, data, state)) return;

	if (data.temporalAccumulateColorTexture && data.temporalAccumulateHistoryColorTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateColorTexture, data.temporalAccumulateHistoryColorTexture);
	if (data.temporalAccumulateMomentTexture && data.temporalAccumulateHistoryMomentTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateMomentTexture, data.temporalAccumulateHistoryMomentTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (!DrawSpatialDenoising(cmd, data, state)) return;

	if (!DrawUpsample(cmd, data, state)) return;

	//if (!DrawScale(data, state)) return;
}

void RTCoreRayTraceReflectPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceReflectOn);

	registry.Store(kDDGIFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;

	// 命中点的漫反射间接光取自 DDGI：与 DDGI 各 Pass 共用同一帧的解析结果（Resolve 按帧缓存、内部加锁）
	if (_sharedState && GetIndirectLightingPolicy(state).ddgiOn)
		registry.Store(kDDGIFrameKey, _sharedState->Resolve(state));

	auto& binding = registry.Get<RayTracingBindingRecord>("binding");

	{
		//光追参数
		RayTraceParams params{
			.tMin = std::max(0.f, state.option.rayTraceReflectParams.tMin),
			.tMax = std::max(0.f, state.option.rayTraceReflectParams.tMax),
			.maxBounce = std::max((uint32_t)1, std::min(state.option.rayTraceReflectParams.maxBounceLimit, GlobalConfig::RayTrace_Max_Bounce_limit)),
			.sampleRayCount = std::max((uint32_t)1, state.option.rayTraceReflectParams.NumSamples),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_RayTraceParamsUBO->WriteData(&params, sizeof(RayTraceParams));
		binding.SetUniformBlock(_RayTraceParamsUBO, 5);
	}

}

void RTCoreRayTraceReflectPass::SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

void RTCoreRayTraceReflectPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState) {
	_sharedState = std::move(sharedState);
}

bool RTCoreRayTraceReflectPass::DrawRayTraceReflect(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state)
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
	target->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	if (!data.hitMotionTexture || data.hitMotionTexture->IsEmpty())
		return false;
	data.hitMotionTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	cmd->clearColorImage(data.hitMotionTexture->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	data.hitMotionTexture->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	auto& binding = registry.Get<RayTracingBindingRecord>("binding");

	CubeParams cubeParams;
	std::shared_ptr<TextureCube> cubeMap;
	std::shared_ptr<TextureCube> IBLDiffuse;
	std::shared_ptr<TextureCube> IBLPrefilter;
	std::shared_ptr<Texture2D> IBLBrdfLUT;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
	{
		if (state.skyAtmosphereParams.skyCube && !state.skyAtmosphereParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyAtmosphereParams.skyCube;
		}

		if (
			state.skyAtmosphereParams.skyCubeDiffuse
			&& !state.skyAtmosphereParams.skyCubeDiffuse->IsEmpty()
			&& state.skyAtmosphereParams.skyCubePrefilter
			&& !state.skyAtmosphereParams.skyCubePrefilter->IsEmpty()
			&& state.skyAtmosphereParams.brdfLUT
			&& !state.skyAtmosphereParams.brdfLUT->IsEmpty()
			)
		{
			cubeParams.IBLEnable = true;
			IBLDiffuse = state.skyAtmosphereParams.skyCubeDiffuse;
			IBLPrefilter = state.skyAtmosphereParams.skyCubePrefilter;
			IBLBrdfLUT = state.skyAtmosphereParams.brdfLUT;
		}
	}
	else if (state.option.flags.skyboxOn)
	{
		if (state.skyboxParams.skyCube && !state.skyboxParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyboxParams.skyCube;
		}

		if (
			state.skyboxParams.skyCubeDiffuse
			&& !state.skyboxParams.skyCubeDiffuse->IsEmpty()
			&& state.skyboxParams.skyCubePrefilter
			&& !state.skyboxParams.skyCubePrefilter->IsEmpty()
			&& state.skyboxParams.brdfLUT
			&& !state.skyboxParams.brdfLUT->IsEmpty()
			)
		{
			cubeParams.IBLEnable = true;
			IBLDiffuse = state.skyboxParams.skyCubeDiffuse;
			IBLPrefilter = state.skyboxParams.skyCubePrefilter;
			IBLBrdfLUT = state.skyboxParams.brdfLUT;
		}
	}

	// 命中点漫反射间接光取 DDGI：DDGI 开启、有可用体积、图集本帧已由 ddgiBlendNode 写好
	std::shared_ptr<const DDGIFrame> ddgiFrame;
	if (auto* framePtr = registry.TryLoad<DDGIFramePtr>(kDDGIFrameKey))
		ddgiFrame = *framePtr;
	registry.Store(kDDGIFrameKey, DDGIFramePtr{});
	auto texReady = [](const std::shared_ptr<Texture2D>& tex) { return tex && !tex->IsEmpty(); };
	const bool ddgiEnable = ddgiFrame && ddgiFrame->enabled && ddgiFrame->uniformBlock && ddgiFrame->volumeTable
		&& texReady(data.ddgiIrradianceAtlas) && texReady(data.ddgiDepthAtlas) && texReady(data.ddgiProbeData);
	cubeParams.DDGIEnable = ddgiEnable ? 1u : 0u;

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(cubeParams));
	paramsUBO->WriteDataAsync(cmd, &cubeParams, sizeof(cubeParams));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);

	if (!BindAccelerationStructure(binding))
		return false;

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	binding.SetUniformBlock(paramsUBO, 6);
	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetStorageImage(data.hitMotionTexture, vk::ImageAspectFlagBits::eColor, 24);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 11);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 12);
	binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 13);
	binding.SetUniformTexture(data.ssaoMap, vk::ImageAspectFlagBits::eColor, 14);

	if (cubeParams.CubeEnable)
		binding.SetUniformTextureCube(cubeMap, vk::ImageAspectFlagBits::eColor, 15);

	if (cubeParams.IBLEnable)
	{
		binding.SetUniformTextureCube(IBLDiffuse, vk::ImageAspectFlagBits::eColor, 16);
		binding.SetUniformTextureCube(IBLPrefilter, vk::ImageAspectFlagBits::eColor, 17);
		binding.SetUniformTexture(IBLBrdfLUT, vk::ImageAspectFlagBits::eColor, 18);
	}

	if (ddgiEnable)
	{
		binding.SetUniformBlock(ddgiFrame->uniformBlock, 19);
		binding.SetStorageBlock(ddgiFrame->volumeTable, 20);
		binding.SetUniformTexture(data.ddgiIrradianceAtlas, vk::ImageAspectFlagBits::eColor, 21);
		binding.SetUniformTexture(data.ddgiDepthAtlas, vk::ImageAspectFlagBits::eColor, 22);
		binding.SetUniformTexture(data.ddgiProbeData, vk::ImageAspectFlagBits::eColor, 23);
	}

	_rayTraceShader.Bind(cmd, binding);
	auto regionData = _rayTraceShader.GetSBTData();
	cmd->traceRaysKHR(regionData.raygenRegion, regionData.missRegion, regionData.hitRegion, regionData.callableRegion, size.x, size.y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceReflectPass::DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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
		.maxAccumulateCount = state.option.rayTraceReflectParams.maxAccumulateCount,
		.depthThreshold = state.option.rayTraceReflectParams.depthThreshold,
		.positiondistThreshold = state.option.rayTraceReflectParams.positiondistThreshold,
		.normalThreshold = state.option.rayTraceReflectParams.normalThreshold,
		.motionWeightScale = state.option.rayTraceReflectParams.motionWeightScale,
		.varianceClipGamma = state.option.rayTraceReflectParams.varianceClipGamma,
		.debugMode = state.option.denoiseDebug.ModeFor(Scene3DRenderOption::DenoiseDebugTarget::RayTraceReflect),
		// 镜面信号：光追端把第一次反弹的命中距离写在 alpha 里，时域累积额外做虚像重投影（反射内容移动时不糊）
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
		data.gMetallicRoughness,
		data.hitMotionTexture		// 命中点运动：反射里的物体在动时虚像跟着移动
	);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceReflectPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.temporalAccumulateColorTexture;
	auto& target = data.denoisedTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	AtrousBilateralFilter::Params params{
		.normalFactor = state.option.rayTraceReflectParams.normalFactor,
		.depthFactor = state.option.rayTraceReflectParams.depthFactor,
		.luminanceFactor = state.option.rayTraceReflectParams.luminanceFactor,
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
		state.option.rayTraceReflectParams.filterCount,
		data.gMetallicRoughness		// 镜面信号：按粗糙度 / 命中距离限制滤波半径
	);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceReflectPass::DrawUpsample(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	// 低分辨率降噪结果 → 全分辨率输出：联合双边上采样（按平面距离 / 法线拒绝跨边缘的点）。
	// 光追端已除以镜面反射率（RayTraceReflect.rgen），这里乘回
	if (!data.denoisedTexture || data.denoisedTexture->IsEmpty() || !data.outPutTexture || data.outPutTexture->IsEmpty())
		return false;

	// 降噪调试：上采样时域累积的调试热力图代替降噪结果（不重新调制），最终合成时替换场景颜色显示
	auto debugTexture = _temporalAccumulate.GetDebugTexture();
	const bool debugView = debugTexture != nullptr;

	_upsample.Execute(
		cmd,
		debugView ? debugTexture : data.denoisedTexture,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gAlbedoOpacity,
		data.gMetallicRoughness,
		data.outPutTexture,
		debugView ? IndirectUpsample::Remodulation::None : IndirectUpsample::Remodulation::Specular,
		state.camera.curUBO,
		state.camera.prevUBO
	);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceReflectPass::DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	return true;
}

void RTCoreRayTraceReflectPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool RTCoreRayTraceReflectPass::BindAccelerationStructure(RayTracingBindingRecord& binding)
{
	if (!_buffers)
		return false;

	binding.SetAccelerationStructure(_buffers->GetAccelerationStructure(), 0);
	binding.SetStorageBlock(_buffers->GetInstancesInfosBlock(), 3);

	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetVertexBlock(), 1);
	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetIndexBlock(), 2);

	// 蒙皮顶点：命中着色器按 instanceInfo.skinned 决定要不要从这里取顶点。
	// 没有蒙皮网格的帧绑静态顶点块兜底（skinned==0 时不会读）。
	auto skinnedVertexBlock = _buffers->GetSkinnedVertexBlock();
	binding.SetStorageBlock(skinnedVertexBlock ? skinnedVertexBlock : IndirectDrawManager::Instance()->GetVertexBlock(), 4);

	return true;
}

void RTCoreRayTraceReflectPass::OnResize(uint32_t width, uint32_t height)
{
	// 渲染图按新尺寸重建，历史纹理是新分配的（内容未定义）：下一帧先清历史
	_firstDrawTemporal = true;
}
