#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SkyBoxPass.h"


SkyBoxPreCalculatePass::SkyBoxPreCalculatePass()
{
	TextureConfig config;
	config.minFilter = vk::Filter::eLinear;
	config.magFilter = vk::Filter::eLinear;
	config.wrapU = vk::SamplerAddressMode::eClampToEdge;
	config.wrapV = vk::SamplerAddressMode::eClampToEdge;
	config.anisotropy = false;
	config.gammaCorrection = false;

	uint32_t skyCubeSize = 256;
	float ratio = float(skyCubeSize) / 8.f;
	uint32_t prefilterLevel = floor(log2(ratio)) + 1;
	prefilterLevel = std::min(prefilterLevel, 8u);

	_skyCubeDiffuse = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
	_skyCubePrefilter = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), prefilterLevel);
}

bool SkyBoxPreCalculatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return !state.option.flags.atmosphereOn && state.option.flags.skyboxOn && state.skyboxParams.cube;
}

void SkyBoxPreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto& skyCube = state.skyboxParams.cube;

	bool shouldUpdate = !_lastSkyCube || _lastSkyCube != skyCube;
	bool shouldCaulateBRDFLUT = _brdfLUT == nullptr;

	if (shouldUpdate || shouldCaulateBRDFLUT)
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();

		{
			IBLImageBuilder::DiffuseGenerateParams diffuseParams{ .samplerCount = 200, .frameIndex = state.renderRecord.frameIndex % 100000 };
			_iblBuilder.CalculateCubeDiffuse(cmd, _skyCubeDiffuse, skyCube, diffuseParams);
		}

		{
			IBLImageBuilder::PrefilterGenerateParams prefilterParams{ .samplerCount = 200, .frameIndex = state.renderRecord.frameIndex % 100000 };
			_iblBuilder.CalculateCubePrefilter(cmd, _skyCubePrefilter, skyCube, prefilterParams);
		}

		{
			if (shouldCaulateBRDFLUT)
			{
				if (!_brdfLUT)
					_brdfLUT = std::make_shared<Texture2D>(512, 512, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
				IBLImageBuilder::BRDFLUTGenerateParams prefilterParams{ .samplerCount = 200, .frameIndex = state.renderRecord.frameIndex % 100000 };
				_iblBuilder.CalculateBRDFLUT(cmd, _brdfLUT, prefilterParams);
			}
		}

		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();

		_lastSkyCube = skyCube;
	}

	state.skyboxParams.hasSkyBoxPreData = true;
	state.skyboxParams.skyCubeDiffuse = _skyCubeDiffuse;
	state.skyboxParams.skyCubePrefilter = _skyCubePrefilter;
	state.skyboxParams.brdfLUT = _brdfLUT;
}

void SkyBoxPreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{}

SkyBoxPass::SkyBoxPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = computeShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddStorageImage(0)
		.AddUnifromTexture(1)
		.AddUnifromTexture(2);

	if (config.Validate())
		_shader.Create(config);

}

SkyBoxPass::~SkyBoxPass()
{}

bool SkyBoxPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return !state.option.flags.atmosphereOn && state.option.flags.skyboxOn && state.skyboxParams.cube;
}

void SkyBoxPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto skyboxCubeMap = state.skyboxParams.cube;
	if (!skyboxCubeMap)
		return;

	auto colorBuffer = ctx.GetExternal(0);
	auto depthBuffer = ctx.GetFrameLocal(0);

	auto cmd = cmdCtx.GetCmd();

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(colorBuffer, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(depthBuffer, vk::ImageAspectFlagBits::eDepth, 1);
	binding.SetUniformTextureCube(skyboxCubeMap, vk::ImageAspectFlagBits::eColor, 2);

	_shader.Bind(cmd, binding);
	cmd->dispatch((state.framebuffer.width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (state.framebuffer.height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

