#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSAOPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

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
		Params params;
		params.kernelSize = 64;
		params.radius = 2.0f;
		params.bias = 0.01f;

		std::vector<glm::vec4> ssaoNoise;
		InitKernelAndNoise(params.samples, ssaoNoise);

		Texture2DConfig config{
			.minFilter = vk::Filter::eNearest,
			.magFilter = vk::Filter::eNearest,
			.wrapU = vk::SamplerAddressMode::eRepeat,
			.wrapV = vk::SamplerAddressMode::eRepeat
		};
		_noiseTexture = std::make_shared<Texture2D>(4, 4, vk::Format::eR32G32B32A32Sfloat, config);
		_noiseTexture->UpdateTextureData(&ssaoNoise[0]);

		_ssaoParams = std::make_shared<UniformBlock>(sizeof(SSAOPass::Params));
		_ssaoParams->WriteData(&params, sizeof(SSAOPass::Params));
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = ssaoComputeShaderPath;

		config
			.AddStorageImage(0)
			.AddUnifromTexture(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromBuffer(4)
			.AddCameraUnifromDataBinding();

		if (config.Validate())
			_ssaoShader.Create(config);
	}


	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = ssaoBlurComputeShaderPath;

		config
			.AddStorageImage(0)
			.AddUnifromTexture(1);

		if (config.Validate())
			_ssaoBlurShader.Create(config);
	}
}

SSAOPass::~SSAOPass()
{
}

void SSAOPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	int width = state.framebuffer.width;
	int height = state.framebuffer.height;

	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto ssaoColorMap = ctx.GetTemp(0);
	auto ssaoBlurColorMap = ctx.GetOutput(0);


	_ssaoShader.SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	_ssaoShader.SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);
	_ssaoShader.SetStorageImage(ssaoColorMap, 0);
	_ssaoShader.SetUniformTexture(gPosition, 1);
	_ssaoShader.SetUniformTexture(gNormal, 2);
	_ssaoShader.SetUniformTexture(_noiseTexture, 3);
	_ssaoShader.SetUniformBlock(_ssaoParams, 4);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	ssaoColorMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Output);

	_ssaoShader.Bind(cmd);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);

	ssaoColorMap->Barrier(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Sample);

	_ssaoBlurShader.SetStorageImage(ssaoBlurColorMap, 0);
	_ssaoBlurShader.SetUniformTexture(ssaoColorMap, 1);

	_ssaoBlurShader.Bind(cmd);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}
