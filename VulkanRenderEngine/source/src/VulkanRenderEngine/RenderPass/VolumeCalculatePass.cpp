#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/AtmospherePreCalculatePass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

// ---------------------------------------------------------------------------------------------------
// 公共部分
// ---------------------------------------------------------------------------------------------------

struct alignas(16) VolumeParams
{
	uint32_t AtmoVolumeEnable = 0;
	uint32_t ExponentialHeightFogEnable = 0;
	uint32_t JitterEnable = 1;
	uint32_t JitterSeed = 0;
	uint32_t CloudEnable = 0;
};

struct alignas(16) VolumetricCloudShaderParams
{
	glm::vec3 cloudAlbedo;
	float cloudBottomHeight;
	glm::vec3 cloudAmbientColor;
	float cloudTopHeight;

	float coverage;
	float density;
	float shapeScale;
	float detailScale;
	float detailStrength;
	float phaseG;
	float phaseBlend;
	float powder;
	float maxTraceDistance;
	float erosionDepth;
	uint32_t viewStepCount;
	uint32_t lightStepCount;
};

static_assert(sizeof(VolumetricCloudShaderParams) == 80);

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

bool VolumePassShouldExecute(RenderState& state)
{
	bool atmoEnable = state.option.flags.atmosphereOn && !state.lights.dirLightInfos.empty();
	return atmoEnable || state.option.flags.exponentialHeightFogOn || state.option.flags.volumetricCloudOn;
}

VolumePassFlags VolumePassFlags::FromState(RenderState& state)
{
	static auto hasVolumeFogLightSource = [](RenderState& state) -> bool {
		for (auto& info : state.lights.dirLightInfos)
		{
			if (info->light && info->light->GetVolumeLightEnable())
				return true;
		}
		for (auto& info : state.lights.pointLightInfos)
		{
			if (info->light && info->light->GetVolumeLightEnable())
				return true;
		}
		for (auto& info : state.lights.spotLightInfos)
		{
			if (info->light && info->light->GetVolumeLightEnable())
				return true;
		}
		return false;
		};

	VolumePassFlags flags;
	flags.atmoEnable = state.option.flags.atmosphereOn
		&& state.skyAtmosphereParams.hasSkyAtmospherePreData
		&& !state.lights.dirLightInfos.empty();
	const bool hasLights = !state.lights.dirLightInfos.empty() || !state.lights.pointLightInfos.empty() || !state.lights.spotLightInfos.empty();
	flags.exponentialHeightFogEnable = state.option.flags.exponentialHeightFogOn;
	flags.volumeFogEnable = flags.exponentialHeightFogEnable
		&& state.option.exponentialHeightFogParams.volumeFogOn
		&& hasLights
		&& hasVolumeFogLightSource(state);
	flags.cloudEnable = state.option.flags.volumetricCloudOn;
	flags.scatterFiltered = flags.atmoEnable || flags.volumeFogEnable || flags.cloudEnable;
	return flags;
}

// binding 0 / 1 / 2：大气、雾、体积开关。Lut 和 Combine 都要
static void UploadVolumeCommonParams(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, ComputeBindingRecord& binding)
{
	bool atmoEnable = state.option.flags.atmosphereOn && !state.lights.dirLightInfos.empty();
	bool hasLights = !state.lights.dirLightInfos.empty() || !state.lights.pointLightInfos.empty() || !state.lights.spotLightInfos.empty();
	bool exponentialHeightFogEnable = state.option.flags.exponentialHeightFogOn;
	bool volumeFogEnable = exponentialHeightFogEnable && state.option.exponentialHeightFogParams.volumeFogOn && hasLights;
	bool cloudEnable = state.option.flags.volumetricCloudOn;

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
	binding.SetUniformBlock(atmosphereParamsUBO, 0);

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
	binding.SetUniformBlock(fogParamsUBO, 1);

	VolumeParams params{
		.AtmoVolumeEnable = (uint32_t)atmoEnable,
		.ExponentialHeightFogEnable = (uint32_t)exponentialHeightFogEnable,
		.JitterEnable = (uint32_t)true,
		.JitterSeed = state.renderRecord.frameIndex % 100000,
		.CloudEnable = (uint32_t)cloudEnable
	};
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
	binding.SetUniformBlock(paramsUBO, 2);
}

// ---------------------------------------------------------------------------------------------------
// VolumeLutPass：散射 / 透射率 + TAA + 高斯
// ---------------------------------------------------------------------------------------------------

VolumeLutPass::VolumeLutPass(const std::string& lutShaderPath, std::shared_ptr<VolumePassShared> shared) :
	_shared(std::move(shared)),
	_firstDrawTemporal(true),
	_taaEnable(false)
{
	// 云噪声：128^3 R8 体积纹理 + 64x64 蓝噪声，都是静态资源
	_cloudShapeNoise = RenderHelper::GetPerlinNoise();
	_blueNoise = RenderHelper::GetBlueNoise();

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
		.AddUnifromTexture(8)
		.AddUnifromBuffer(9)		// 云参数
		.AddUnifromTexture(10)		// sampler3D 形状噪声
		.AddUnifromTexture(11);		// sampler2D 蓝噪声

	if (config.Validate())
		_lutShader.Create(config);
}

bool VolumeLutPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return VolumePassShouldExecute(state);
}

void VolumeLutPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!VolumePassShouldExecute(state))
	{
		SetTAAEnable(false);
		return;
	}

	auto& lutBinding = registry.Get<ComputeBindingRecord>("lutBinding");
	auto cmd = VKCONTEXT->GetCommandBuffer();

	UploadVolumeCommonParams(cmd, state, lutBinding);

	const auto& cloudOptions = state.option.volumetricCloudParams;
	VolumetricCloudShaderParams cloudParams
	{
		.cloudAlbedo = cloudOptions.cloudAlbedo,
		.cloudBottomHeight = cloudOptions.cloudBottomHeight,
		.cloudAmbientColor = cloudOptions.cloudAmbientColor,
		.cloudTopHeight = cloudOptions.cloudTopHeight,
		.coverage = std::clamp(cloudOptions.coverage, 0.0f, 0.999f),
		.density = std::max(cloudOptions.density, 0.0f),
		.shapeScale = cloudOptions.shapeScale,
		.detailScale = cloudOptions.detailScale,
		.detailStrength = std::clamp(cloudOptions.detailStrength, 0.0f, 1.0f),
		.phaseG = cloudOptions.phaseG,
		.phaseBlend = cloudOptions.phaseBlend,
		.powder = cloudOptions.powder,
		.maxTraceDistance = cloudOptions.maxTraceDistance,
		.erosionDepth = cloudOptions.erosionDepth,
		.viewStepCount = std::max(1u, cloudOptions.viewStepCount),
		.lightStepCount = std::max(1u, cloudOptions.lightStepCount)
	};
	auto cloudParamsUBO = std::make_shared<UniformBlock>(sizeof(cloudParams));
	cloudParamsUBO->WriteDataAsync(cmd, &cloudParams, sizeof(cloudParams));
	lutBinding.SetUniformBlock(cloudParamsUBO, 9);

	cmd->SubmitNowAndWait();
}

void VolumeLutPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	_shared->lutValid.store(false, std::memory_order_relaxed);

	FrameRenderData data;
	data.flags = VolumePassFlags::FromState(state);
	if (!data.flags.Any())
		return;

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.atlasShadowMap = ctx.GetInput(2);
	data.gMotionVector = ctx.GetInput(3);
	data.gDepthStencil = ctx.GetFrameLocal(0);

	data.scatterColor = ctx.GetOutput(0);
	data.tramsmittance = ctx.GetOutput(1);
	data.gaussScatterColor = ctx.GetOutput(2);

	data.taaScatterColor = ctx.GetTemp(0);
	data.taaHistoryScatterColor = ctx.GetPersitent(0);
	data.gPrevPosition = ctx.GetPersitent(1);
	data.gPrevNormal = ctx.GetPersitent(2);
	data.gPrevDepthStencil = ctx.GetPersitent(3);

	auto cmd = cmdCtx.GetCmd();

	DrawLut(cmd, registry, state, data);

	SetTAAEnable(data.flags.scatterFiltered);
	if (data.flags.scatterFiltered)
	{
		if (!DrawTAAFilter(cmd, state, data))
			return;

		bool copyToHistory = Texture2D::CopyTextureAsync(cmd, data.taaScatterColor, data.taaHistoryScatterColor);
		if (cmd->IsRecording())
			cmd->SubmitToQueue();
		if (!copyToHistory)
			return;

		if (!DrawGaussFilter(cmd, state, data))
			return;
	}

	_shared->lutValid.store(true, std::memory_order_release);
}

void VolumeLutPass::DrawLut(
	std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderGraph::FrameDataRegistry& registry,
	RenderState& state,
	FrameRenderData& data
)
{
	uint32_t width = data.scatterColor->GetWidth();
	uint32_t height = data.scatterColor->GetHeight();
	auto& binding = registry.Get<ComputeBindingRecord>("lutBinding");

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);

	binding.SetStorageImage(data.scatterColor, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetStorageImage(data.tramsmittance, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 5);
	if (data.flags.atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.transmittanceLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 7);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 8);

	if (_cloudShapeNoise)
		binding.SetUniformTexture3D(_cloudShapeNoise, vk::ImageAspectFlagBits::eColor, 10);
	if (_blueNoise)
		binding.SetUniformTexture(_blueNoise, vk::ImageAspectFlagBits::eColor, 11);

	_lutShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

bool VolumeLutPass::DrawTAAFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data)
{
	auto& sourceScatter = data.scatterColor;
	auto& historyScatter = data.taaHistoryScatterColor;
	auto& targetScatter = data.taaScatterColor;

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

bool VolumeLutPass::DrawGaussFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data)
{
	auto& source = data.taaScatterColor;
	auto& target = data.gaussScatterColor;

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

void VolumeLutPass::SetTAAEnable(bool enable) const
{
	if (_taaEnable == enable)
		return;
	_taaEnable = enable;
	if (_taaEnable)
		_firstDrawTemporal = true;
}

void VolumeLutPass::OnResize(uint32_t width, uint32_t height)
{
	// 渲染图按新尺寸重建，历史纹理是新分配的（内容未定义）：下一帧先清历史
	_firstDrawTemporal = true;
}

// ---------------------------------------------------------------------------------------------------
// VolumeCombinePass：和场景颜色合成
// ---------------------------------------------------------------------------------------------------

VolumeCombinePass::VolumeCombinePass(const std::string& combinShaderPath, std::shared_ptr<VolumePassShared> shared) :
	_shared(std::move(shared))
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

bool VolumeCombinePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return VolumePassShouldExecute(state);
}

void VolumeCombinePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!VolumePassShouldExecute(state))
		return;

	auto& combinBinding = registry.Get<ComputeBindingRecord>("combinBinding");
	auto cmd = VKCONTEXT->GetCommandBuffer();
	UploadVolumeCommonParams(cmd, state, combinBinding);
	cmd->SubmitNowAndWait();
}

void VolumeCombinePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	// Lut 这一帧没算完（没执行 / 滤波失败）：不合成，场景颜色保持原样（和拆分前 return 的行为一致）
	const bool lutValid = _shared->lutValid.exchange(false, std::memory_order_acquire);
	if (!lutValid)
		return;

	const VolumePassFlags flags = VolumePassFlags::FromState(state);
	if (!flags.Any())
		return;

	auto target = ctx.GetExternal(0);
	auto scatterColor = ctx.GetInput(0);
	auto tramsmittance = ctx.GetInput(1);
	auto gaussScatterColor = ctx.GetInput(2);
	auto depth = ctx.GetFrameLocal(0);
	auto tempSceneColor = ctx.GetTemp(0);
	if (!target || !scatterColor || !tramsmittance || !gaussScatterColor || !depth || !tempSceneColor)
		return;

	auto cmd = cmdCtx.GetCmd();

	uint32_t width = target->GetWidth();
	uint32_t height = target->GetHeight();

	Texture2D::CopyTextureAsync(cmd, target, tempSceneColor);
	tempSceneColor->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	auto& binding = registry.Get<ComputeBindingRecord>("combinBinding");

	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(tempSceneColor, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(depth, vk::ImageAspectFlagBits::eDepth, 5);
	if (flags.atmoEnable)
	{
		binding.SetUniformTexture(state.skyAtmosphereParams.skyViewLut, vk::ImageAspectFlagBits::eColor, 6);
	}
	// 跑了滤波就读滤波后的散射；只开非体积高度雾时散射全是 0，没跑滤波，读原始散射（滤波结果这一帧没写过）
	if (flags.scatterFiltered)
		binding.SetUniformTexture(gaussScatterColor, vk::ImageAspectFlagBits::eColor, 7);
	else
		binding.SetUniformTexture(scatterColor, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(tramsmittance, vk::ImageAspectFlagBits::eColor, 8);

	_combinShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}
