#include "OpenGLRenderEngine/RenderPass/DepthFogPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "glm/gtc/matrix_transform.hpp"

DepthFogPass::DepthFogPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

bool DepthFogPass::ShouldExecute(RenderState& state) const
{
	return state.option.flags.depthFogOn;
}

void DepthFogPass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto sceneColorBuffer = ctx.GetExternal(0);
	auto sceneDepthBuffer = ctx.GetExternal(1);

	auto tempColor = ctx.GetTemp(0);
	auto tempDepth = ctx.GetTemp(1);

	Texture2D::CopyTexture(sceneColorBuffer, tempColor);
	Texture2D::CopyTexture(sceneDepthBuffer, tempDepth);

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);
	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	_shader.setTexture(tempColor, "colorMap", 10);
	_shader.setTexture(tempDepth, "depthMap", 11);

	_shader.setVec3("fogColor", state.option.depthFogParams.fogColor);
	_shader.setFloat("fogHeight", state.option.depthFogParams.fogHeight);

	_shader.setFloat("fogDistanceFalloff", state.option.depthFogParams.fogDistanceFalloff);
	_shader.setFloat("fogHeightFalloff", state.option.depthFogParams.fogHeightFalloff);

	RenderHelp::renderScreenQuad();
}