#include "OpenGLRenderEngine/RenderPass/LightingPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"

LightingPass::LightingPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

LightingPass::~LightingPass() {}

void LightingPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto gMetallicRoughness = ctx.GetInput(3);
	auto atlasShadowMap = ctx.GetInput(4);
	auto ssao = ctx.GetInput(5);

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	_shader.setTexture(gPosition, "gPosition", 5);
	_shader.setTexture(gNormal, "gNormal", 6);
	_shader.setTexture(gAlbedoOpacity, "gAlbedoOpacity", 7);
	_shader.setTexture(gMetallicRoughness, "gMetallicRoughness", 8);
	_shader.setTexture(atlasShadowMap, "atlasShadowMap", 9);
	_shader.setTexture(ssao, "ssao", 10);

	RenderHelp::SetupLightingData(
		_shader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	RenderHelp::renderScreenQuad();
}

void LightingPass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}
