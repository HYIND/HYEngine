#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/AtmospherePreCalculatePass.h"

VolumeCalculatePass::VolumeCalculatePass(
	const std::string& lutShaderPath,
	const std::string& combinShaderPath
) :
	_firstDrawTemporal(true),
	_enable(false)
{
	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = lutShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddLightDataBinding()
			.AddUnifromBuffer(0)
			.AddUnifromBuffer(1)
			.AddUnifromBuffer(2)
			.AddStorageImage(3)
			.AddStorageImage(4)
			.AddUnifromTexture(5)
			.AddUnifromTexture(6)
			.AddUnifromTexture(7)
			.AddUnifromTexture(8);

		if (config.Validate())
			_lutShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = combinShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddUnifromBuffer(1)
			.AddUnifromBuffer(2)
			.AddStorageImage(3)
			.AddUnifromTexture(4)
			.AddUnifromTexture(5)
			.AddUnifromTexture(6)
			.AddUnifromTexture(7)
			.AddUnifromTexture(8);

		if (config.Validate())
			_combinShader.Create(config);
	}
}

bool VolumeCalculatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	bool atmoEnable = state.option.flags.atmosphereOn && !state.lights.dirLightInfos.empty();
	return atmoEnable || state.option.flags.exponentialHeightFogOn;
}

struct VolumeParams
{
	uint32_t AtmoVolumeEnable = 0;
	uint32_t ExponentialHeightFogEnable = 0;
	uint32_t JitterEnable = 1;
	uint32_t JitterSeed = 0;
};

struct alignas(16) ExponentialHeightFogShaderParams
{
	glm::vec3 fogAlbedo;
	float globalDensity;
	float fogStartHeight;
	float fogStartDistance;
	float fogHeightFalloff;
	uint32_t VolumeFogEnable;
	float FogMaxOpacity;
	uint32_t VolumeLightScatterPathSampleCount;
};

static_assert(sizeof(ExponentialHeightFogShaderParams) == 48);

void VolumeCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	bool enable = ShouldExecute(registry, state);
	SetEnable(enable);
	if (!enable)
		return;

	bool atmoEnable = state.option.flags.atmosphereOn && !state.lights.dirLightInfos.empty();
	bool hasLights = !state.lights.dirLightInfos.empty() || !state.lights.pointLightInfos.empty() || !state.lights.spotLightInfos.empty();
	bool exponentialHeightFogEnable = state.option.flags.exponentialHeightFogOn;
	bool volumeFogEnable = exponentialHeightFogEnable && state.option.exponentialHeightFogParams.volumeFogOn && hasLights;

	auto& lutBinding = *registry.Get<ComputeBindingRecord>("lutBinding");
	auto& combinBinding = *registry.Get<ComputeBindingRecord>("combinBinding");
	auto cmd = VKCONTEXT->GetCommandBuffer();

	glm::vec3 dirLightColor(0.0f);
	glm::vec3 dirLightDirection(0.0f, -1.0f, 0.0f);
	if (!state.lights.dirLightInfos.empty())
	{
		auto& dirLight = state.lights.dirLightInfos[0]->light;
		dirLightColor = dirLight->GetColor() * dirLight->GetIntensity();
		dirLightDirection = dirLight->GetDirection();
	}

	Atmo::AtmosphereParams atmosphereParams
	{
		.DirLightColor = dirLightColor,
		.DirLightDir = dirLightDirection,
		.CubeCapturePosition = state.option.atmosphereParams.CubeCapturePosition,
		.PlanetRadius = state.option.atmosphereParams.PlanetRadius,
		.AtmosphereHeight = state.option.atmosphereParams.AtmosphereHeight,
		.RayleighScatteringScalarHeight = state.option.atmosphereParams.RayleighScatteringScalarHeight,
		.MieScatteringScalarHeight = state.option.atmosphereParams.MieScatteringScalarHeight,
		.MieAnisotropy = state.option.atmosphereParams.MieAnisotropy,
		.OzoneLevelCenterHeight = state.option.atmosphereParams.OzoneLevelCenterHeight,
		.OzoneLevelWidth = state.option.atmosphereParams.OzoneLevelWidth,
		.ScatterPathSampleCount = state.option.atmosphereParams.ScatterPathSampleCount,
		.TransmittanceSampleCount = state.option.atmosphereParams.TransmittanceSampleCount
	};
	auto atmosphereParamsUBO = std::make_shared<UniformBlock>(sizeof(atmosphereParams));
	atmosphereParamsUBO->WriteDataAsync(cmd, &atmosphereParams, sizeof(atmosphereParams));
	lutBinding.SetUniformBlock(atmosphereParamsUBO, 0);
	combinBinding.SetUniformBlock(atmosphereParamsUBO, 0);

	const auto& fogOptions = state.option.exponentialHeightFogParams;
	ExponentialHeightFogShaderParams fogParams
	{
		.fogAlbedo = fogOptions.fogAlbedo,
		.globalDensity = fogOptions.globalDensity / 100.0f,
		.fogStartHeight = fogOptions.fogStartHeight,
		.fogStartDistance = fogOptions.fogStartDistance,
		.fogHeightFalloff = fogOptions.fogHeightFalloff / 100.0f,
		.VolumeFogEnable = volumeFogEnable,
		.FogMaxOpacity = std::clamp(fogOptions.fogMaxOpacity, 0.0f, 1.0f),
		.VolumeLightScatterPathSampleCount = std::max(1u, fogOptions.VolumeLightScatterPathSampleCount)
	};
	auto fogParamsUBO = std::make_shared<UniformBlock>(sizeof(fogParams));
	fogParamsUBO->WriteDataAsync(cmd, &fogParams, sizeof(fogParams));
	lutBinding.SetUniformBlock(fogParamsUBO, 1);
	combinBinding.SetUniformBlock(fogParamsUBO, 1);

	{
		VolumeParams params{
			.AtmoVolumeEnable = (uint32_t)atmoEnable,
			.ExponentialHeightFogEnable = (uint32_t)exponentialHeightFogEnable,
			.JitterEnable = (uint32_t)true,
			.JitterSeed = state.renderRecord.frameIndex % 100000
		};
		auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
		paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
		lutBinding.SetUniformBlock(paramsUBO, 2);
		combinBinding.SetUniformBlock(paramsUBO, 2);
	}

	cmd->SubmitNowAndWait();
}

void VolumeCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	bool atmoEnable = state.option.flags.atmosphereOn
		&& state.skyAtmosphereParams.hasSkyAtmospherePreData
		&& !state.lights.dirLightInfos.empty();
	bool hasLights = !state.lights.dirLightInfos.empty() || !state.lights.pointLightInfos.empty() || !state.lights.spotLightInfos.empty();
	bool exponentialHeightFogEnable = state.option.flags.exponentialHeightFogOn;
	bool volumeFogEnable = exponentialHeightFogEnable && state.option.exponentialHeightFogParams.volumeFogOn && hasLights;

	if (!atmoEnable && !exponentialHeightFogEnable)
		return;

	FrameRenderData data;
	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gDepthStencil = ctx.GetFrameLocal(0);
	data.atlasShadowMap = ctx.GetInput(2);
	data.gMotionVector = ctx.GetInput(3);
	data.gPrevPosition = ctx.GetInput(4);
	data.gPrevNormal = ctx.GetInput(5);
	data.gPrevDepthStencil = ctx.GetInput(6);

	data.targetSceneColor = ctx.GetExternal(0);

	data.tempScatterColor = ctx.GetTemp(0);
	data.tempTramsmittance = ctx.GetTemp(1);
	data.tempSceneColor = ctx.GetTemp(2);
	data.taaScatterColorTexture = ctx.GetTemp(3);
	data.taaGaussScatterColorTexture = ctx.GetTemp(4);

	data.taaHistoryScatterColorTexture = ctx.GetPersitent(0);

	data.atmoEnable = atmoEnable;

	auto cmd = cmdCtx.GetCmd();

	DrawLut(cmd, registry, state, data);
	if (!DrawTAAFilter(cmd, state, data))
		return;

	bool copyToHistory = Texture2D::CopyTextureAsync(cmd, data.taaScatterColorTexture, data.taaHistoryScatterColorTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();
	if (!copyToHistory)
		return;

	if (!DrawGaussFilter(cmd, state, data))
		return;

	CombinVolume(cmd, registry, state, data);
}

void VolumeCalculatePass::DrawLut(
	std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderGraph::FrameDataRegistry& registry,
	RenderState& state,
	FrameRenderData& data
)
{
	uint32_t width = data.tempScatterColor->GetWidth();
	uint32_t height = data.tempScatterColor->GetHeight();
	auto& binding = *registry.Get<ComputeBindingRecord>("lutBinding");

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);

	binding.SetStorageImage(data.tempScatterColor, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetStorageImage(data.tempTramsmittance, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 5);
	if (data.atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.transmittanceLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 7);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 8);

	_lutShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

bool VolumeCalculatePass::DrawTAAFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data)
{
	auto& sourceScatter = data.tempScatterColor;
	auto& historyScatter = data.taaHistoryScatterColorTexture;
	auto& targetScatter = data.taaScatterColorTexture;

	if (_firstDrawTemporal)
	{
		bool result = Texture2D::CopyTextureAsync(cmd, sourceScatter, targetScatter);
		if (cmd->IsRecording())
			cmd->SubmitToQueue();
		if (result)
			_firstDrawTemporal = false;
		return result;
	}

	TAAFilter::Params params{
	};

	_taaFilter.Execute(
		cmd,
		sourceScatter,
		historyScatter,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gMotionVector,
		data.gPrevPosition,
		data.gPrevNormal,
		data.gPrevDepthStencil,
		targetScatter,
		params,
		state.camera.curUBO,
		state.camera.prevUBO
	);

	cmd->SubmitToQueue();

	return true;
}

bool VolumeCalculatePass::DrawGaussFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data)
{
	auto& source = data.taaScatterColorTexture;
	auto& target = data.taaGaussScatterColorTexture;

	AtrousGaussFilter::Params params{
		.sigma = 1.0,
		.kernelRadius = 2,
		.kernelStep = 1
	};

	_gaussFilter.Execute(
		cmd,
		source,
		target,
		params
	);

	cmd->SubmitToQueue();

	return true;
}

void VolumeCalculatePass::CombinVolume(
	std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderGraph::FrameDataRegistry& registry,
	RenderState& state,
	FrameRenderData& data
)
{
	auto& target = data.targetSceneColor;

	uint32_t width = target->GetWidth();
	uint32_t height = target->GetHeight();

	Texture2D::CopyTextureAsync(cmd, target, data.tempSceneColor);
	data.tempSceneColor->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	auto& binding = *registry.Get<ComputeBindingRecord>("combinBinding");

	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(data.tempSceneColor, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 5);
	if (data.atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.skyViewLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	binding.SetUniformTexture(data.taaGaussScatterColorTexture, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(data.tempTramsmittance, vk::ImageAspectFlagBits::eColor, 8);

	_combinShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

void VolumeCalculatePass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

