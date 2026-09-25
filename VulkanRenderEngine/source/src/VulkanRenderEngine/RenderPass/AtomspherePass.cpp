#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/AtomspherePass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;


AtomspherePass::AtomspherePass(
	const std::string& computeShaderPath,
	const std::string& transmittanceLutShaderPath,
	const std::string& skyViewLutLutShaderPath
)
{
	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = computeShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4)
			.AddUnifromTexture(5);

		if (config.Validate())
			_shader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
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
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
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
		Texture2DConfig config;
		config.minFilter = vk::Filter::eLinear;
		config.magFilter = vk::Filter::eLinear;
		config.wrapU = vk::SamplerAddressMode::eClampToEdge;
		config.wrapV = vk::SamplerAddressMode::eClampToEdge;
		config.anisotropy = false;
		config.gammaCorrection = false;
		_transmittanceLut = std::make_shared<Texture2D>(256, 256, vk::Format::eR16G16B16A16Sfloat, config);
		_skyViewLut = std::make_shared<Texture2D>(512, 512, vk::Format::eR16G16B16A16Sfloat, config);
	}

}

bool AtomspherePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.atomsphereOn && !state.lights.dirLightInfos.empty();
}

void AtomspherePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(AtomsphereParams));

	auto& dirLight = state.lights.dirLightInfos[0]->light;

	AtomsphereParams params
	{
		.DirLightColor = dirLight->getColor() * dirLight->getIntensity(),
		.DirLightDir = dirLight->getDirection(),
		.PlanetRadius = state.option.atomsphereParams.PlanetRadius,
		.AtmosphereHeight = state.option.atomsphereParams.AtmosphereHeight,
		.RayleighScatteringScalarHeight = state.option.atomsphereParams.RayleighScatteringScalarHeight,
		.MieScatteringScalarHeight = state.option.atomsphereParams.MieScatteringScalarHeight,
		.MieAnisotropy = state.option.atomsphereParams.MieAnisotropy,
		.OzoneLevelCenterHeight = state.option.atomsphereParams.OzoneLevelCenterHeight,
		.OzoneLevelWidth = state.option.atomsphereParams.OzoneLevelWidth,
		.ScatterPathSampleCount = state.option.atomsphereParams.ScatterPathSampleCount,
		.TransmittanceSampleCount = state.option.atomsphereParams.TransmittanceSampleCount
	};

	paramsUBO->WriteData(&params, sizeof(AtomsphereParams));
	binding.SetUniformBlock(paramsUBO, 0);

	static auto shouldUpdateTransmittanceLut = [](AtomsphereParams& params1, AtomsphereParams& params2) ->bool {
		return params1.PlanetRadius != params2.PlanetRadius
			|| params1.AtmosphereHeight != params2.AtmosphereHeight
			|| params1.RayleighScatteringScalarHeight != params2.RayleighScatteringScalarHeight
			|| params1.MieScatteringScalarHeight != params2.MieScatteringScalarHeight
			|| params1.OzoneLevelCenterHeight != params2.OzoneLevelCenterHeight
			|| params1.OzoneLevelWidth != params2.OzoneLevelWidth
			|| params1.TransmittanceSampleCount != params2.TransmittanceSampleCount;
		};

	if (!_lastParams || shouldUpdateTransmittanceLut(params, *_lastParams))
	{
		if (!_lastParams)
			_lastParams = std::make_shared<AtomsphereParams>();
		*_lastParams = params;
		CaulateTransmittanceLut(binding);
	}

	CaulateSkyViewLut(binding, state);
}

void AtomspherePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto sceneColorBuffer = ctx.GetExternal(0);
	auto sceneDepthBuffer = ctx.GetExternal(1);

	auto tempColor = ctx.GetTemp(0);

	if (!sceneColorBuffer
		|| !sceneDepthBuffer
		|| !tempColor
		|| sceneColorBuffer->IsEmpty()
		|| sceneDepthBuffer->IsEmpty()
		|| tempColor->IsEmpty()
		)
		return;

	auto cmd = cmdCtx.GetCmd();

	Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColor);

	uint32_t width = sceneColorBuffer->GetWidth();
	uint32_t height = sceneColorBuffer->GetHeight();


	auto& binding = *registry.Get<ComputeBindingRecord>("binding");

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(sceneColorBuffer, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(tempColor, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 3);
	binding.SetUniformTexture(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(_skyViewLut, vk::ImageAspectFlagBits::eColor, 5);

	_shader.Bind(cmd, binding);
	cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();
}

void AtomspherePass::CaulateTransmittanceLut(ComputeBindingRecord& binding)
{
	if (!_transmittanceLut || _transmittanceLut->IsEmpty())
		return;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	binding.SetStorageImage(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 1);

	_transmittanceLutShader.Bind(cmd, binding);
	cmd->dispatch((_transmittanceLut->GetWidth() + work_size_x - 1) / work_size_x, (_transmittanceLut->GetHeight() + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitNowAndWait();
}

void AtomspherePass::CaulateSkyViewLut(ComputeBindingRecord& binding, RenderState& state)
{
	if (!_skyViewLut || _skyViewLut->IsEmpty())
		return;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(_skyViewLut, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(_transmittanceLut, vk::ImageAspectFlagBits::eColor, 2);

	_skyViewLutshader.Bind(cmd, binding);
	cmd->dispatch((_skyViewLut->GetWidth() + work_size_x - 1) / work_size_x, (_skyViewLut->GetHeight() + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitNowAndWait();
}
