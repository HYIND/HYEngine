#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/AtmospherePreCalculatePass.h"

using namespace Atmo;

AtmospherePreCalculatePass::AtmospherePreCalculatePass(
	const std::string& transmittanceLutShaderPath,
	const std::string& skyViewLutLutShaderPath,
	const std::string& skyCubeGenerateShaderPath
)
{
	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = transmittanceLutShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddStorageImage(1);

		if (config.Validate())
			_transmittanceLutShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = skyViewLutLutShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_skyViewLutshader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = skyCubeGenerateShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage2DArray(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_skyCubeGenerateShader.Create(config);
	}


	{
		TextureConfig config;
		config.minFilter = vk::Filter::eLinear;
		config.magFilter = vk::Filter::eLinear;
		config.wrapU = vk::SamplerAddressMode::eClampToEdge;
		config.wrapV = vk::SamplerAddressMode::eClampToEdge;
		config.anisotropy = false;
		config.gammaCorrection = false;
		_transmittanceLut = std::make_shared<Texture2D>(256, 256, vk::Format::eR16G16B16A16Sfloat, config);
		_skyViewLut = std::make_shared<Texture2D>(256, 256, vk::Format::eR16G16B16A16Sfloat, config);


		uint32_t skyCubeSize = 256;
		float ratio = float(skyCubeSize) / 8.f;
		uint32_t prefilterLevel = floor(log2(ratio)) + 1;
		prefilterLevel = std::min(prefilterLevel, 8u);

		// 天空 cube 带完整 mip：IBL 预过滤 / 漫反射按立体角从低分辨率 mip 取值（太阳这类小而亮的区域靠它摊开）
		const uint32_t skyCubeLevels = static_cast<uint32_t>(floor(log2(float(skyCubeSize)))) + 1;
		_skyCube = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), skyCubeLevels);
		// 漫反射辐照度很平滑，每面 32 就够（原来 256）
		constexpr uint32_t diffuseCubeSize = 32;
		_skyCubeDiffuse = std::make_shared<TextureCube>(diffuseCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
		_skyCubePrefilter = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), prefilterLevel);
	}

}

bool AtmospherePreCalculatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.atmosphereOn && !state.lights.dirLightInfos.empty();
}

void AtmospherePreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto& dirLight = state.lights.dirLightInfos[0]->light;

	AtmosphereParams params
	{
		.DirLightColor = dirLight->GetColor() * dirLight->GetIntensity(),
		.DirLightDir = dirLight->GetDirection(),
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

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteData(&params, sizeof(params));

	ComputeBindingRecord binding;
	binding.SetUniformBlock(paramsUBO, 0);

	static auto GetShouldUpdateTransmittanceLut = [](AtmosphereParams& params1, AtmosphereParams& params2) ->bool {
		return params1.PlanetRadius != params2.PlanetRadius
			|| params1.AtmosphereHeight != params2.AtmosphereHeight
			|| params1.RayleighScatteringScalarHeight != params2.RayleighScatteringScalarHeight
			|| params1.MieScatteringScalarHeight != params2.MieScatteringScalarHeight
			|| params1.OzoneLevelCenterHeight != params2.OzoneLevelCenterHeight
			|| params1.OzoneLevelWidth != params2.OzoneLevelWidth
			|| params1.TransmittanceSampleCount != params2.TransmittanceSampleCount;
		};

	static auto GetShouldUpdateSkyCube = [](AtmosphereParams& params1, AtmosphereParams& params2) ->bool {
		return  params1.DirLightColor != params2.DirLightColor
			|| params1.DirLightDir != params2.DirLightDir
			|| params1.CubeCapturePosition != params2.CubeCapturePosition
			|| params1.MieAnisotropy != params2.MieAnisotropy
			|| params1.ScatterPathSampleCount != params2.ScatterPathSampleCount;
		};

	bool shouldUpdateTransmittanceLut = !_lastParams || GetShouldUpdateTransmittanceLut(params, *_lastParams);
	bool shouldUpdateSkyCube = shouldUpdateTransmittanceLut || GetShouldUpdateSkyCube(params, *_lastParams);
	bool shouldCaulateBRDFLUT = _brdfLUT == nullptr;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	if (shouldUpdateTransmittanceLut)
	{
		CaulateTransmittanceLut(cmd, binding);
		_transmittanceLut->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	}

	if (shouldUpdateSkyCube || shouldCaulateBRDFLUT)
	{

		{
			ComputeBindingRecord skyCubeBinding;
			skyCubeBinding.SetUniformBlock(paramsUBO, 0);
			CaulateSkyCube(cmd, skyCubeBinding);
			_skyCube->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
		}

		// 生成天空 cube 的 mip：GenerateTextureMipMaps 自己取命令缓冲并立即提交，先把前面录的提交掉，之后换一个新的继续录
		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();
		_skyCube->GenerateTextureMipMaps();
		cmd = VKCONTEXT->GetCommandBuffer();

		{
			IBLImageBuilder::DiffuseGenerateParams diffuseParams{};
			_iblBuilder.CalculateCubeDiffuse(cmd, _skyCubeDiffuse, _skyCube, diffuseParams);
		}

		{
			IBLImageBuilder::PrefilterGenerateParams prefilterParams{};
			_iblBuilder.CalculateCubePrefilter(cmd, _skyCubePrefilter, _skyCube, prefilterParams);
		}

		{
			if (shouldCaulateBRDFLUT)
			{
				if (!_brdfLUT)
					_brdfLUT = std::make_shared<Texture2D>(512, 512, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
				IBLImageBuilder::BRDFLUTGenerateParams lutParams{};
				_iblBuilder.CalculateBRDFLUT(cmd, _brdfLUT, lutParams);
			}
		}

	}


	CaulateSkyViewLut(cmd, binding, state);

	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();

	if (shouldUpdateTransmittanceLut || shouldUpdateSkyCube)
	{
		if (!_lastParams)
			_lastParams = std::make_shared<AtmosphereParams>();
		*_lastParams = params;
	}

	state.skyAtmosphereParams.hasSkyAtmospherePreData = true;
	state.skyAtmosphereParams.transmittanceLut = _transmittanceLut;
	state.skyAtmosphereParams.skyViewLut = _skyViewLut;
	state.skyAtmosphereParams.skyCube = _skyCube;
	state.skyAtmosphereParams.skyCubeDiffuse = _skyCubeDiffuse;
	state.skyAtmosphereParams.skyCubePrefilter = _skyCubePrefilter;
	state.skyAtmosphereParams.brdfLUT = _brdfLUT;
}

void AtmospherePreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{}

void AtmospherePreCalculatePass::CaulateTransmittanceLut(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding)
{
	if (!_transmittanceLut || _transmittanceLut->IsEmpty())
		return;

	binding.SetStorageImage(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 1);

	_transmittanceLutShader.Bind(cmd, binding);
	cmd->dispatch((_transmittanceLut->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (_transmittanceLut->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);
}

void AtmospherePreCalculatePass::CaulateSkyViewLut(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding, RenderState& state)
{
	if (!_skyViewLut || _skyViewLut->IsEmpty())
		return;

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(_skyViewLut, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 2);

	_skyViewLutshader.Bind(cmd, binding);
	cmd->dispatch((_skyViewLut->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (_skyViewLut->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);
}

void AtmospherePreCalculatePass::CaulateSkyCube(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding)
{
	if (!_skyCube || _skyCube->IsEmpty())
		return;

	// 只写第 0 级（storage image 的视图只能有一个 mip），其余级之后 GenerateTextureMipMaps 生成
	binding.SetStorageImage2DArrayLevel(_skyCube, vk::ImageAspectFlagBits::eColor, 0, 1, 1);
	binding.SetUniformTexture(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 2);

	_skyCubeGenerateShader.Bind(cmd, binding);
	cmd->dispatch(
		(_skyCube->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(_skyCube->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		std::min(6u, _skyCube->GetLayerCount())
	);
}
