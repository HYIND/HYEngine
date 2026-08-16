#include "OpenGLRenderEngine/RenderPass/CombinPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include <format>

CombinPass::CombinPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
	: _width(SCR_WIDTH), _height(SCR_HEIGHT)
{
	_shader.AddDefineMacro("COMBIN_MODE", 0);
	_shader.CompileFromFile(vertexShaderPath, fragmentShaderPath);
}

void CombinPass::Draw(GLuint destFbo, const std::vector<GLuint>& colorBuffers)
{
	if (destFbo == 0 || colorBuffers.empty())
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, destFbo);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glViewport(0, 0, _width, _height);

	_shader.Use();

	int count = std::min(Max_Color_Buffer_Count, (int)colorBuffers.size());
	for (int i = 0; i < colorBuffers.size(); i++)
	{
		std::string name = std::format("ColorMap[{}]", i);
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
		_shader.setInt(name, i);
	}
	_shader.setInt("ColorMapCount", count);

	RenderHelp::renderScreenQuad();
}

void CombinPass::OnResize(uint32_t newWidth, uint32_t newHeight)
{
	_width = newWidth;
	_height = newHeight;
}

