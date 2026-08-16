#include "OpenGLRenderEngine/RenderPass/BloomPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"

BloomPass::BloomPass(const std::string& bloomVertexShaderPath, const std::string& bloomFragmentShaderPath, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
	:bloomBlurShader(bloomVertexShaderPath.c_str(), bloomFragmentShaderPath.c_str()), SCR_WIDTH(SCR_WIDTH), SCR_HEIGHT(SCR_HEIGHT)
{
	pingpongFBO[0] = pingpongFBO[1] = 0;
	pingpongColorBuffers[0] = pingpongColorBuffers[1] = 0;
	outPutTarget = 0;
	init();
}

void BloomPass::Draw(GLuint brightColorBuffer)
{
	glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

	bloomBlurShader.Use();
	bloomBlurShader.setInt("image", 0);

	bool horizontal = true, first_iteration = true;
	unsigned int amount = 10;
	for (int i = 0; i < amount; i++)
	{
		bloomBlurShader.setInt("horizontal", horizontal);

		int index_cur = horizontal ? 0 : 1;
		int index_difference = horizontal ? 1 : 0;

		glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[index_cur]);
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glActiveTexture(GL_TEXTURE0);
		if (first_iteration)
			glBindTexture(GL_TEXTURE_2D, brightColorBuffer);
		else
			glBindTexture(GL_TEXTURE_2D, pingpongColorBuffers[index_difference]);

		RenderHelp::renderScreenQuad();

		outPutTarget = pingpongColorBuffers[index_cur];

		horizontal = !horizontal;
		if (first_iteration)
			first_iteration = false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint BloomPass::GetBloomBlurMap()
{
	return outPutTarget;
}

void BloomPass::OnResize(uint32_t newWidth, uint32_t newHeight)
{
	if (SCR_WIDTH == newWidth && SCR_HEIGHT == newHeight)
		return;

	SCR_WIDTH = newWidth;
	SCR_HEIGHT = newHeight;
	init();
}

void BloomPass::init()
{
	GLuint pingpongFBO1, pingpongColorBuffer1, pingpongFBO2, pingpongColorBuffer2;

	static auto genernate = [](GLuint& fbo, GLuint& colorBuffer, float SCR_WIDTH, float SCR_HEIGHT)-> void
		{
			if (fbo == 0)
				glGenFramebuffers(1, &fbo);
			if (colorBuffer == 0)
				glGenTextures(1, &colorBuffer);

			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glBindTexture(GL_TEXTURE_2D, colorBuffer);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

			// also check if framebuffers are complete (no need for depth buffer)
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "initbloomblurdata Framebuffer not complete!" << std::endl;
		};

	genernate(pingpongFBO[0], pingpongColorBuffers[0], SCR_WIDTH, SCR_HEIGHT);
	genernate(pingpongFBO[1], pingpongColorBuffers[1], SCR_WIDTH, SCR_HEIGHT);
	outPutTarget = pingpongColorBuffers[0];
}
