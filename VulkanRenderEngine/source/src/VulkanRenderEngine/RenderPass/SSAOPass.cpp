#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSAOPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"


struct alignas(16) KernelParams {
	std::array<glm::vec4, 64> samples;
	uint32_t kernelSize;
};

struct alignas(16) Params {
	float radius = 20.f;
	float bias = 0.05f;
};

static float Lerp(float a, float b, float f)
{
	return a + f * (b - a);
}

static void InitKernelAndNoise(std::array<glm::vec4, 64>& kernel, std::vector<glm::vec4>& ssaoNoise)
{
	std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
	std::default_random_engine generator;
	for (unsigned int i = 0; i < 64; ++i)
	{
		glm::vec4 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator), 0.f);
		sample = glm::normalize(sample);
		sample *= randomFloats(generator);
		float scale = float(i) / 64.0f;

		// scale samples s.t. they're more aligned to center of kernel
		scale = Lerp(0.1f, 1.0f, scale * scale);
		sample *= scale;
		kernel[i] = sample;
	}

	for (unsigned int i = 0; i < 16; i++)
	{
		glm::vec4 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f, 0.f); // rotate around z-axis (in tangent space)
		ssaoNoise.push_back(noise);
	}
}

SSAOPass::SSAOPass(
	const std::string& ssaoComputeShaderPath,
	const std::string& ssaoBlurComputeShaderPath
)
{

	{
		KernelParams kernelParams;
		kernelParams.kernelSize = 64;

		std::vector<glm::vec4> ssaoNoise;
		InitKernelAndNoise(kernelParams.samples, ssaoNoise);

		TextureConfig config{
			.minFilter = vk::Filter::eNearest,
			.magFilter = vk::Filter::eNearest,
			.wrapU = vk::SamplerAddressMode::eRepeat,
			.wrapV = vk::SamplerAddressMode::eRepeat
		};
		_noiseTexture = std::make_shared<Texture2D>(4, 4, vk::Format::eR32G32B32A32Sfloat, config);
		_noiseTexture->UpdateTextureData(&ssaoNoise[0]);

		_kernelParams = std::make_shared<UniformBlock>(sizeof(kernelParams));
		_kernelParams->WriteData(&kernelParams, sizeof(kernelParams));
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = ssaoComputeShaderPath;

		config
			.AddStorageImage(0)
			.AddUnifromTexture(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4)
			.AddUnifromBuffer(5)
			.AddUnifromBuffer(6)
			.AddCameraUnifromDataBinding();

		if (config.Validate())
			_ssaoShader.Create(config);
	}


	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = ssaoBlurComputeShaderPath;

		config
			.AddStorageImage(0)
			.AddUnifromTexture(1);

		if (config.Validate())
			_ssaoBlurShader.Create(config);
	}
}

SSAOPass::~SSAOPass()
{}

void SSAOPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{

	Params params{
		.radius = state.option.ssaoParams.radius,
		.bias = state.option.ssaoParams.bias
	};

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteData(&params, sizeof(params));

	binding.SetUniformBlock(_kernelParams, 5);
	binding.SetUniformBlock(paramsUBO, 6);
}

void SSAOPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	uint32_t width = state.framebuffer.width;
	uint32_t height = state.framebuffer.height;

	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto ssaoColorMap = ctx.GetTemp(0);
	auto ssaoBlurColorMap = ctx.GetOutput(0);

	auto gDepthStencil = ctx.GetFrameLocal(0);

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");
	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(ssaoColorMap, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(gPosition, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(gNormal, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(gDepthStencil, vk::ImageAspectFlagBits::eDepth, 3);
	binding.SetUniformTexture(_noiseTexture, vk::ImageAspectFlagBits::eColor, 4);

	auto cmd = cmdCtx.GetCmd();

	ssaoColorMap->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	_ssaoShader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	ssaoColorMap->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	ComputeBindingRecord ssaoBlurBinding;
	ssaoBlurBinding.SetStorageImage(ssaoBlurColorMap, vk::ImageAspectFlagBits::eColor, 0);
	ssaoBlurBinding.SetUniformTexture(ssaoColorMap, vk::ImageAspectFlagBits::eColor, 1);

	_ssaoBlurShader.Bind(cmd, ssaoBlurBinding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}
