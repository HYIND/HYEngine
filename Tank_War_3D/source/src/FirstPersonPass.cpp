#include "OpenGLRenderEngine/RenderPass/FirstPersonPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "glm/gtc/matrix_transform.hpp"

FirstPersonPass::FirstPersonPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

void FirstPersonPass::Draw(RenderState& state)
{
	//if (state.objects.firstPersonItems.empty())
	//	return;

	////glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer.firstPersonFbo);

	//glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClearStencil(0);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	//_shader.Use();

	//RenderHelp::renderFirstPersonScene(state, _shader, state.objects.firstPersonItems);
}