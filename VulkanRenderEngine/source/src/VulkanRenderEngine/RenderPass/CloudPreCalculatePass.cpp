#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/CloudPreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/AtmospherePreCalculatePass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

namespace
{
	struct alignas(16) CloudCubeRuntimeParams
	{
		uint32_t AtmoVolumeEnable = 0;	// 只控制太阳光的大气透射 LUT 衰减，与背景选择无关
		uint32_t JitterEnable = 0;
		uint32_t JitterSeed = 0;
		uint32_t Reserved = 0;
	};
}

CloudPreCalculatePass::CloudPreCalculatePass(const std::string& cubeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = cubeShaderPath;

	config
		.AddLightDataBinding()				// set 3：dirLightMetaData，CloudLighting 读太阳光
		.AddUnifromBuffer(0)				// AtmoParams（AtmoPublic.comp）
		.AddUnifromBuffer(2)				// CloudCubeRuntimeParams
		.AddUnifromTexture(6)				// 大气 transmittanceLut
		.AddUnifromBuffer(9)				// CloudParams
		.AddUnifromTexture(10)				// sampler3D 形状噪声
		.AddUnifromTexture(11)				// sampler2D 蓝噪声
		.AddUnifromTexture(13)				// samplerCube 天空辐照度（iblParams.skyDiffuse，环境项来源）
		.AddUnifromTexture(14)				// sampler2D 大气多重散射 LUT（AtmoVolume 公共函数依赖）
		.AddStorageImage2DArray(12);		// 输出云 cube（RGB=S，A=T）

	if (config.Validate())
		_cubeGenerateShader.Create(config);

	_cloudShapeNoise = RenderHelper::GetPerlinNoise();
	_blueNoise = RenderHelper::GetBlueNoise();

	const uint32_t cloudCubeSize = 128;
	const uint32_t cloudCubeLevels = static_cast<uint32_t>(floor(log2(float(cloudCubeSize)))) + 1;
	_cloudCube = std::make_shared<TextureCube>(cloudCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), cloudCubeLevels);
	_dummyTransmittanceLut = std::make_shared<Texture2D>(1, 1, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());

	const glm::vec4 zeroTexel(0.0f);
	_dummyTransmittanceLut->UpdateTextureData(const_cast<void*>(static_cast<const void*>(&zeroTexel)), 0);
	_dummyTransmittanceLut->MakeSampledImmutable();
}

CloudCubeShaderParams CloudPreCalculatePass::BuildCloudParams(const RenderState& state)
{
	const auto& p = state.option.volumetricCloudParams;
	CloudCubeShaderParams params{};
	params.cloudAlbedo = p.cloudAlbedo;
	params.cloudBottomHeight = p.cloudBottomHeight;
	params.cloudTopHeight = p.cloudTopHeight;
	params.coverage = std::clamp(p.coverage, 0.0f, 0.999f);
	params.density = std::max(p.density, 0.0f);
	params.shapeScale = p.shapeScale;
	params.detailScale = p.detailScale;
	params.detailStrength = std::clamp(p.detailStrength, 0.0f, 1.0f);
	params.phaseG = p.phaseG;
	params.multiScatterContribution = std::clamp(p.multiScatterContribution, 0.0f, 1.0f);
	params.multiScatterOcclusion = std::clamp(p.multiScatterOcclusion, 0.01f, 1.0f);
	params.multiScatterEccentricity = std::clamp(p.multiScatterEccentricity, 0.0f, 1.0f);
	params.powder = p.powder;
	params.maxTraceDistance = p.maxTraceDistance;
	params.erosionDepth = p.erosionDepth;
	params.viewStepCount = std::max(1u, p.viewStepCount);
	params.lightStepCount = std::max(1u, p.lightStepCount);
	params.multiScatterOctaves = std::clamp(p.multiScatterOctaves, 1u, 4u);
	params.skyFillStrength = std::clamp(p.skyFillStrength, 0.0f, 2.0f);
	params.skyFillDesaturation = std::clamp(p.skyFillDesaturation, 0.0f, 1.0f);
	params.skyFillBottomOcclusion = std::clamp(p.skyFillBottomOcclusion, 0.0f, 1.0f);
	return params;
}

bool CloudPreCalculatePass::SameParams(const CloudCubeShaderParams& a, const CloudCubeShaderParams& b)
{
	return glm::all(glm::equal(a.cloudAlbedo, b.cloudAlbedo))
		&& a.cloudBottomHeight == b.cloudBottomHeight
		&& a.cloudTopHeight == b.cloudTopHeight
		&& a.coverage == b.coverage
		&& a.density == b.density
		&& a.shapeScale == b.shapeScale
		&& a.detailScale == b.detailScale
		&& a.detailStrength == b.detailStrength
		&& a.phaseG == b.phaseG
		&& a.multiScatterContribution == b.multiScatterContribution
		&& a.multiScatterOcclusion == b.multiScatterOcclusion
		&& a.multiScatterEccentricity == b.multiScatterEccentricity
		&& a.powder == b.powder
		&& a.maxTraceDistance == b.maxTraceDistance
		&& a.erosionDepth == b.erosionDepth
		&& a.viewStepCount == b.viewStepCount
		&& a.lightStepCount == b.lightStepCount
		&& a.multiScatterOctaves == b.multiScatterOctaves
		&& a.skyFillStrength == b.skyFillStrength
		&& a.skyFillDesaturation == b.skyFillDesaturation
		&& a.skyFillBottomOcclusion == b.skyFillBottomOcclusion;
}

void CloudPreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!state.option.flags.volumetricCloudOn)
	{
		state.cloudCubeParams = {};
		_generated = false;
		return;
	}

	// 可选的大气透射 LUT，
	std::shared_ptr<Texture2D> transmittanceLut;
	const auto& atmo = state.skyAtmosphereParams;
	if (atmo.hasSkyAtmospherePreData && atmo.transmittanceLut && !atmo.transmittanceLut->IsEmpty())
		transmittanceLut = atmo.transmittanceLut;

	const CloudCubeShaderParams params = BuildCloudParams(state);
	const bool needUpdate = !_generated
		|| !SameParams(params, _lastParams)
		|| transmittanceLut != _lastTransmittanceLut
		|| atmo.transmittanceRevision != _lastTransmittanceRevision
		|| state.iblParams.skyDiffuseRevision != _lastSkyDiffuseRevision;

	if (needUpdate)
	{
		auto cmd = cmdCtx.GetCmd();

		// AtmoParams 即使大气关闭也要绑定：AtmoPublic.comp 的 PlanetRadius / 采样数在 shader 里被引用
		Atmo::AtmosphereParams atmoParams{};
		if (!state.lights.dirLightInfos.empty() && state.lights.dirLightInfos[0] && state.lights.dirLightInfos[0]->light)
		{
			auto light = state.lights.dirLightInfos[0]->light;
			atmoParams.DirLightColor = light->GetColor() * light->GetIntensity();
			atmoParams.DirLightDir = light->GetDirection();
		}
		const auto& src = state.option.atmosphereParams;
		atmoParams.CubeCapturePosition = src.CubeCapturePosition;
		atmoParams.PlanetRadius = src.PlanetRadius;
		atmoParams.AtmosphereHeight = src.AtmosphereHeight;
		atmoParams.RayleighScatteringScalarHeight = src.RayleighScatteringScalarHeight;
		atmoParams.MieScatteringScalarHeight = src.MieScatteringScalarHeight;
		atmoParams.MieAnisotropy = src.MieAnisotropy;
		atmoParams.OzoneLevelCenterHeight = src.OzoneLevelCenterHeight;
		atmoParams.OzoneLevelWidth = src.OzoneLevelWidth;
		atmoParams.ScatterPathSampleCount = src.ScatterPathSampleCount;
		atmoParams.TransmittanceSampleCount = src.TransmittanceSampleCount;

		auto atmoUBO = std::make_shared<UniformBlock>(sizeof(atmoParams));
		atmoUBO->WriteDataAsync(cmd, &atmoParams, sizeof(atmoParams));

		CloudCubeRuntimeParams runtime{};
		runtime.AtmoVolumeEnable = transmittanceLut ? 1u : 0u;
		runtime.JitterEnable = 0u;	// 云 cube 是稳定资产，不能引入逐帧抖动
		runtime.JitterSeed = 0u;
		auto runtimeUBO = std::make_shared<UniformBlock>(sizeof(runtime));
		runtimeUBO->WriteDataAsync(cmd, &runtime, sizeof(runtime));

		auto cloudParamsUBO = std::make_shared<UniformBlock>(sizeof(params));
		cloudParamsUBO->WriteDataAsync(cmd, &params, sizeof(params));

		ComputeBindingRecord binding;
		binding.SetUniformBlock(atmoUBO, 0);
		binding.SetLightStorageData(
			state.lights.ssbo_dirLightMeta,
			state.lights.ssbo_dirLightCascade,
			state.lights.ssbo_pointLightMeta,
			state.lights.ssbo_spotLightMeta,
			state.lights.ssbo_dirLightCascadeDistances);
		binding.SetUniformBlock(runtimeUBO, 2);
		binding.SetUniformTexture(transmittanceLut ? transmittanceLut : _dummyTransmittanceLut, vk::ImageAspectFlagBits::eColor, 6);
		binding.SetUniformBlock(cloudParamsUBO, 9);
		if (_cloudShapeNoise)
			binding.SetUniformTexture3D(_cloudShapeNoise, vk::ImageAspectFlagBits::eColor, 10);
		if (_blueNoise)
			binding.SetUniformTexture(_blueNoise, vk::ImageAspectFlagBits::eColor, 11);
		if (state.iblParams.skyDiffuse && !state.iblParams.skyDiffuse->IsEmpty())
			binding.SetUniformTextureCube(state.iblParams.skyDiffuse, vk::ImageAspectFlagBits::eColor, 13);
		if (state.skyAtmosphereParams.multiScatteringLut && !state.skyAtmosphereParams.multiScatteringLut->IsEmpty())
			binding.SetUniformTexture(state.skyAtmosphereParams.multiScatteringLut, vk::ImageAspectFlagBits::eColor, 14);
		else
			binding.SetUniformTexture(_dummyTransmittanceLut, vk::ImageAspectFlagBits::eColor, 14);
		binding.SetStorageImage2DArrayLevel(_cloudCube, vk::ImageAspectFlagBits::eColor, 0, 1, 12);

		_cubeGenerateShader.Bind(cmd, binding);
		cmd->dispatch(
			(_cloudCube->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
			(_cloudCube->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
			6);

		_cloudCube->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
		_cloudCube->GenerateTextureMipMapsAsync(cmd);
		cmd->SubmitToQueue();

		_lastParams = params;
		_lastTransmittanceLut = transmittanceLut;
		_lastTransmittanceRevision = atmo.transmittanceRevision;
		_lastSkyDiffuseRevision = state.iblParams.skyDiffuseRevision;
		_generated = true;
		++_cloudCubeRevision;
	}

	state.cloudCubeParams.hasCloudPreData = _generated;
	state.cloudCubeParams.skyCube = _generated ? _cloudCube : nullptr;
	state.cloudCubeParams.skyCubeRevision = _cloudCubeRevision;
}
