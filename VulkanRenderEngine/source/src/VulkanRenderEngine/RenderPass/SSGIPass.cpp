#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSGIPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"


struct CubeParams {
	uint32_t CubeEnable = 0;
};

struct SSGIParams
{
	float tMin;
	float tMax;
	uint32_t maxBounce = 0;
	uint32_t SampleRayCount;
	uint32_t RayMarchingMaxStep;
	float GIIntensity;
	float AOIntensity;
	float DistanceFactor;
	uint32_t frameIndex;
};


SSGIPass::SSGIPass(const std::string& computerShaderPath)
	:
	_enable(false)
{

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.AddDefineMacro("Max_Bounce_limit", GlobalConfig::SSTrace_Max_Bounce_limit);
		config.computePath = computerShaderPath;

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
			.AddStorageImage(12)		// 镜面那一路的追踪结果（alpha = 第一次反弹的命中距离）
			.AddUnifromBuffer(13);		// 反射部分高度雾（ReflectionFog.comp，和主画面同一份参数）

		if (config.Validate())
			_ssgiShader.Create(config);
	}

	_SSGIParamsUBO = std::make_shared<UniformBlock>(sizeof(SSGIParams));
}

bool SSGIPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssgiTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSGIPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
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
	data.ssaoTexture = ctx.GetInput(8);
	data.gPrevPosition = ctx.GetPersitent(4);
	data.gPrevNormal = ctx.GetPersitent(5);
	data.gPrevDepthStencil = ctx.GetPersitent(6);

	data.gDepthStencil = ctx.GetFrameLocal(0);

	// Temp 0 ~ 4：漫反射（追踪 / 时域颜色 / 时域矩 / 空域乒乓 / 空域结果），5 ~ 9：镜面，顺序相同
	// Persistent 0 ~ 1：漫反射历史（颜色 / 矩），2 ~ 3：镜面历史，4 ~ 6：上一帧 G-Buffer
	auto bindSignal = [&](IndirectSignalDenoiser::Textures& tex, uint32_t tempBase, uint32_t persistentBase) {
		tex.origin = ctx.GetTemp(tempBase + 0);
		tex.temporalColor = ctx.GetTemp(tempBase + 1);
		tex.temporalMoment = ctx.GetTemp(tempBase + 2);
		tex.spatialTemp = ctx.GetTemp(tempBase + 3);
		tex.output = ctx.GetTemp(tempBase + 4);
		tex.historyColor = ctx.GetPersitent(persistentBase + 0);
		tex.historyMoment = ctx.GetPersitent(persistentBase + 1);
	};
	bindSignal(data.diffuse, 0, 0);
	bindSignal(data.specular, 5, 2);

	data.outPutTexture = ctx.GetOutput(0);

	data.sceneColorMap = ctx.GetExternal(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawSSGI(cmd, data, registry, state)) return;

	if (!DrawDenoise(cmd, data, state)) return;

	// 降噪调试：用漫反射那一路的调试热力图覆盖输出，最终合成时替换场景颜色显示
	if (auto debugTexture = _diffuseDenoiser.GetDebugTexture(); debugTexture && data.outPutTexture)
	{
		Texture2D::CopyTextureAsync(cmd, debugTexture, data.outPutTexture);
		if (cmd->IsRecording())
			cmd->SubmitToQueue();
		return;
	}

	DrawCombine(cmd, data, state);
}

void SSGIPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.ssgiOn);

	if (!ShouldExecute(registry, state))
		return;

	auto& binding = registry.Get<ComputeBindingRecord>("binding");

	{

		SSGIParams params{
			.tMin = std::max(0.f, state.option.ssgiTraceParams.tMin),
			.tMax = std::max(0.f, state.option.ssgiTraceParams.tMax),
			.maxBounce = std::max(1u, std::min(state.option.ssgiTraceParams.maxBounceLimit, GlobalConfig::SSTrace_Max_Bounce_limit)),
			.SampleRayCount = state.option.ssgiTraceParams.NumSamples,
			.RayMarchingMaxStep = std::max(2u, state.option.ssgiTraceParams.RayMarchingMaxStep),
			.GIIntensity = std::max(0.01f, state.option.ssgiTraceParams.GIIntensity),
			.AOIntensity = std::max(0.01f, state.option.ssgiTraceParams.AOIntensity),
			.DistanceFactor = std::max(0.0001f, state.option.ssgiTraceParams.DistanceFactor),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_SSGIParamsUBO->WriteData(&params, sizeof(SSGIParams));
		binding.SetUniformBlock(_SSGIParamsUBO, 0);
	}
}

void SSGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
	{
		_diffuseDenoiser.ResetHistory();
		_specularDenoiser.ResetHistory();
	}
}

bool SSGIPass::DrawSSGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& target = data.diffuse.origin;
	auto& specularTarget = data.specular.origin;

	if (!target || target->IsEmpty() || !specularTarget || specularTarget->IsEmpty())
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
	specularTarget->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	cmd->clearColorImage(specularTarget->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	auto& binding = registry.Get<ComputeBindingRecord>("binding");

	CubeParams cubeParams;
	std::shared_ptr<TextureCube> cubeMap;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
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

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(cubeParams));
	paramsUBO->WriteDataAsync(cmd, &cubeParams, sizeof(cubeParams));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);


	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetUniformBlock(paramsUBO, 1);
	binding.SetUniformBlock(CreateReflectionFogUniformBlock(cmd, state, state.option.ssgiTraceParams.fogVolumeMode), 13);
	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetStorageImage(specularTarget, vk::ImageAspectFlagBits::eColor, 12);
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


	_ssgiShader.Bind(cmd, binding);
	cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSGIPass::DrawDenoise(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	const auto& giParams = state.option.ssgiTraceParams;

	IndirectSignalDenoiser::GBuffer gbuffer{
		.position = data.gPosition,
		.normal = data.gNormal,
		.depth = data.gDepthStencil,
		.motionVector = data.gMotionVector,
		.prevPosition = data.gPrevPosition,
		.prevNormal = data.gPrevNormal,
		.prevDepth = data.gPrevDepthStencil,
		.metallicRoughness = data.gMetallicRoughness
	};

	TemporalAccumulate::Params diffuseParams{
		.maxAccumulateCount = giParams.maxAccumulateCount,
		.depthThreshold = giParams.depthThreshold,
		.positiondistThreshold = giParams.positiondistThreshold,
		.normalThreshold = giParams.normalThreshold,
		.motionWeightScale = giParams.motionWeightScale,
		.varianceClipGamma = giParams.varianceClipGamma,
		// 降噪调试看漫反射那一路（GI 的主体）
		.debugMode = state.option.denoiseDebug.ModeFor(Scene3DRenderOption::DenoiseDebugTarget::SSGI)
	};

	// 镜面那一路：alpha 是第一次反弹的命中距离，时域累积额外做虚像重投影
	TemporalAccumulate::Params specularParams = diffuseParams;
	specularParams.debugMode = 0;
	specularParams.signalType = static_cast<uint32_t>(TemporalAccumulate::SignalType::Specular);

	AtrousBilateralFilter::Params filterParams{
		.normalFactor = giParams.normalFactor,
		.depthFactor = giParams.depthFactor,
		.luminanceFactor = giParams.luminanceFactor
	};

	if (!_diffuseDenoiser.Execute(cmd, gbuffer, data.diffuse, diffuseParams, filterParams, giParams.filterCount, state.camera.curUBO, state.camera.prevUBO))
		return false;
	// 镜面那一路：按粗糙度 / 命中距离限制空域滤波半径（光滑表面的反射不被抹糊）
	AtrousBilateralFilter::Params specularFilterParams = filterParams;
	specularFilterParams.specularMode = 1;
	return _specularDenoiser.Execute(cmd, gbuffer, data.specular, specularParams, specularFilterParams, giParams.filterCount, state.camera.curUBO, state.camera.prevUBO);
}

bool SSGIPass::DrawCombine(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	// SSGI.comp 已把两路分别除以漫反射率 / 镜面反射率，这里各自乘回再相加（同分辨率，上采样退化为逐像素）
	const auto& diffuse = data.diffuse.output;
	const auto& specular = data.specular.output;
	if (!diffuse || diffuse->IsEmpty() || !specular || specular->IsEmpty() || !data.outPutTexture || data.outPutTexture->IsEmpty())
		return false;

	_combine.Execute(
		cmd,
		diffuse,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gAlbedoOpacity,
		data.gMetallicRoughness,
		data.outPutTexture,
		IndirectUpsample::Remodulation::DiffuseSpecular,
		state.camera.curUBO,
		state.camera.prevUBO,
		specular
	);

	cmd->SubmitToQueue();

	return true;
}

void SSGIPass::OnResize(uint32_t width, uint32_t height)
{
	// 渲染图按新尺寸重建，历史纹理是新分配的（内容未定义）：下一帧先清历史
	_diffuseDenoiser.ResetHistory();
	_specularDenoiser.ResetHistory();
}
