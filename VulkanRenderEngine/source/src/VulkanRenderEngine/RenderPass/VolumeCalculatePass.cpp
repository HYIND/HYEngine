#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"

VolumeCalculatePass::VolumeCalculatePass(
	const std::string& lutShaderPath,
	const std::string& combinShaderPath
)
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
};

static_assert(sizeof(ExponentialHeightFogShaderParams) == 48);

void VolumeCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
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
		.FogMaxOpacity = std::clamp(fogOptions.fogMaxOpacity, 0.0f, 1.0f)
	};
	auto fogParamsUBO = std::make_shared<UniformBlock>(sizeof(fogParams));
	fogParamsUBO->WriteDataAsync(cmd, &fogParams, sizeof(fogParams));
	lutBinding.SetUniformBlock(fogParamsUBO, 1);
	combinBinding.SetUniformBlock(fogParamsUBO, 1);

	{
		VolumeParams params{
			.AtmoVolumeEnable = atmoEnable,
			.ExponentialHeightFogEnable = exponentialHeightFogEnable
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

	auto sceneColorBuffer = ctx.GetExternal(0);
	auto sceneDepthBuffer = ctx.GetFrameLocal(0);

	auto tempColor = ctx.GetTemp(0);
	auto tempScatterColor = ctx.GetTemp(1);
	auto tempTramsmittance = ctx.GetTemp(2);

	auto atlasShadowMap = ctx.GetInput(0);
	auto gPosition = ctx.GetInput(1);

	if (!sceneColorBuffer
		|| !sceneDepthBuffer
		|| !tempColor
		|| sceneColorBuffer->IsEmpty()
		|| sceneDepthBuffer->IsEmpty()
		|| tempColor->IsEmpty()
		)
		return;

	auto cmd = cmdCtx.GetCmd();

	DrawLut(
		cmd,
		registry,
		state,
		tempScatterColor,
		tempTramsmittance,
		sceneDepthBuffer,
		atlasShadowMap,
		gPosition,
		atmoEnable
	);

	CombinVolume(
		cmd,
		registry,
		state,
		sceneColorBuffer,
		tempColor,
		sceneDepthBuffer,
		tempScatterColor,
		tempTramsmittance,
		atmoEnable
	);

}

void VolumeCalculatePass::DrawLut(
	std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderGraph::FrameDataRegistry& registry,
	RenderState& state,
	std::shared_ptr<Texture2D>& tempScatterColor,
	std::shared_ptr<Texture2D>& tempTramsmittance,
	std::shared_ptr<Texture2D>& sceneDepthBuffer,
	std::shared_ptr<Texture2D>& atlasShadowMap,
	std::shared_ptr<Texture2D>& gPosition,
	bool atmoEnable
)
{
	uint32_t width = tempScatterColor->GetWidth();
	uint32_t height = tempScatterColor->GetHeight();
	auto& binding = *registry.Get<ComputeBindingRecord>("lutBinding");

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);

	binding.SetStorageImage(tempScatterColor, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetStorageImage(tempTramsmittance, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 5);
	if (atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.transmittanceLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	binding.SetUniformTexture(atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 7);
	binding.SetUniformTexture(gPosition, vk::ImageAspectFlagBits::eColor, 8);

	_lutShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

void VolumeCalculatePass::CombinVolume(
	std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderGraph::FrameDataRegistry& registry,
	RenderState& state,
	std::shared_ptr<Texture2D>& sceneColorBuffer,
	std::shared_ptr<Texture2D>& tempColor,
	std::shared_ptr<Texture2D>& sceneDepthBuffer,
	std::shared_ptr<Texture2D>& tempScatterColor,
	std::shared_ptr<Texture2D>& tempTramsmittance,
	bool atmoEnable
)
{
	Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColor);
	tempColor->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	uint32_t width = sceneColorBuffer->GetWidth();
	uint32_t height = sceneColorBuffer->GetHeight();
	auto& binding = *registry.Get<ComputeBindingRecord>("combinBinding");

	binding.SetStorageImage(sceneColorBuffer, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(tempColor, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 5);
	if (atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.skyViewLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	binding.SetUniformTexture(tempScatterColor, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(tempTramsmittance, vk::ImageAspectFlagBits::eColor, 8);

	_combinShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

