#include "OpenGLRenderEngine/RenderPass/SSAOPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include <random>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

float Lerp(float a, float b, float f)
{
	return a + f * (b - a);
}

SSAOPass::SSAOPass(
	const std::string& ssaoVertexShaderPath, const std::string& ssaoFragmentShaderPath,
	const std::string& ssaoBlurVertexShaderPath, const std::string& ssaoBlurFragmentShaderPath)
	:ssaoShader(ssaoVertexShaderPath, ssaoFragmentShaderPath), ssaoBlurShader(ssaoBlurVertexShaderPath, ssaoBlurFragmentShaderPath)
{
	glGenFramebuffers(1, &ssaoFBO);
	glGenFramebuffers(1, &ssaoBlurFBO);

	std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0);
	std::default_random_engine generator;
	for (unsigned int i = 0; i < 64; ++i)
	{
		glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
		sample = glm::normalize(sample);
		sample *= randomFloats(generator);
		float scale = float(i) / 64.0f;

		// scale samples s.t. they're more aligned to center of kernel
		scale = Lerp(0.1f, 1.0f, scale * scale);
		sample *= scale;
		ssaoKernel.push_back(sample);
	}

	std::vector<glm::vec3> ssaoNoise;
	for (unsigned int i = 0; i < 16; i++)
	{
		glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f); // rotate around z-axis (in tangent space)
		ssaoNoise.push_back(noise);
	}

	noiseTexture = std::make_unique<Texture2D>(4, 4, GL_RGB32F);
	noiseTexture->SetFiltering(GL_NEAREST)
		.SetWrapping(GL_REPEAT)
		.UpdateTextureData(&ssaoNoise[0], GL_RGB, GL_FLOAT);
}

SSAOPass::~SSAOPass()
{
	if (ssaoFBO != 0)
		glDeleteFramebuffers(1, &ssaoFBO);
	if (ssaoBlurFBO != 0)
		glDeleteFramebuffers(1, &ssaoBlurFBO);
}

void SSAOPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	int width = state.framebuffer.width;
	int height = state.framebuffer.height;

	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto ssaoColorMap = ctx.GetTemp(0);
	auto ssaoBlurColorMap = ctx.GetOutput(0);

	BindToFbo(ssaoColorMap, ssaoBlurColorMap);

	glViewport(0, 0, width, height);

	glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
	ssaoShader.Use();

	ssaoShader.setTexture(gPosition, "gPosition", 0);
	ssaoShader.setTexture(gNormal, "gNormal", 1);
	ssaoShader.setTexture(noiseTexture, "texNoise", 2);

	int kernelSize = std::min((size_t)64, ssaoKernel.size());
	for (int i = 0; i < kernelSize; i++)
	{
		std::string name = std::format("samples[{}]", i);
		ssaoShader.setVec3(name, ssaoKernel[i]);
	}
	ssaoShader.setInt("kernelSize", kernelSize);
	ssaoShader.setFloat("radius", 2.0);
	ssaoShader.setFloat("bias", 0.01);

	ssaoShader.setVec2("noiseScale", glm::vec2(width / 4.0, height / 4.0));

	RenderHelp::renderScreenQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
	ssaoBlurShader.Use();
	ssaoBlurShader.setTexture(ssaoColorMap, "ssaoInput", 0);
	RenderHelp::renderScreenQuad();

}

void SSAOPass::BindToFbo(std::shared_ptr<Texture2D>& ssaoColorBuffer, std::shared_ptr<Texture2D>& ssaoBlurColorBuffer)
{
	glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
	ssaoColorBuffer->Bind();
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBuffer->GetID(), 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "SSAO Framebuffer not complete!" << std::endl;

	glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
	ssaoBlurColorBuffer->Bind();
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoBlurColorBuffer->GetID(), 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "SSAO Blur Framebuffer not complete!" << std::endl;
}
