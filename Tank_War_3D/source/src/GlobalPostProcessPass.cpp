#include "OpenGLRenderEngine/RenderPass/GlobalPostProcessPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"

GlobalPostProcessPass::GlobalPostProcessPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath, float SCR_WIDTH, float SCR_HEIGHT)
	:_shader(vertexShaderPath, fragmentShaderPath), _width(SCR_WIDTH), _height(SCR_HEIGHT)
{
}

void GlobalPostProcessPass::Draw(GLuint fbo, GLuint colorBuffer, GLuint _bloomBlurMap,
	bool bloom_on, bool gamma_on, bool needFlipFinalFboY,
	float exposureValue, float gammaValue
)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glClearColor(0.f, 0.f, 0.f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glViewport(0, 0, _width, _height);

	_shader.Use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	_shader.setInt("colorBuffer", 0);

	if (bloom_on)
	{
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, _bloomBlurMap);
		_shader.setInt("bloomMap", 1);
	}
	_shader.setBool("bloomEnable", bloom_on);

	_shader.setFloat("exposureValue", exposureValue);
	_shader.setBool("gammaEnable", gamma_on);
	_shader.setFloat("gammaValue", gammaValue);

	_shader.setBool("filpY", needFlipFinalFboY);

	RenderHelp::renderScreenQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
