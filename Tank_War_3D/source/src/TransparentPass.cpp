#include "OpenGLRenderEngine/RenderPass/TransparentPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "Manager/ResourceManager.h"
#include "glm/gtc/matrix_transform.hpp"

TransparentPass::TransparentPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

bool TransparentPass::ShouldExecute(RenderState& state) const
{
	return state.flags.drawTransparent && !state.objects.sceneTransparentItems.empty();
}

void TransparentPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto atlasShadowMap = ctx.GetInput(0);
	if (!atlasShadowMap)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	_shader.setTexture(atlasShadowMap, "atlasShadowMap", 9);

	RenderHelp::SetupLightingData(_shader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	std::sort(state.objects.sceneTransparentItems.begin(), state.objects.sceneTransparentItems.end(),
		[&](const OpenGLRender::SceneTransparentItem& item1, const OpenGLRender::SceneTransparentItem& item2)-> bool
		{
			auto aabb1 = item1.meshinfo.mesh->GetAABB();
			auto aabb2 = item2.meshinfo.mesh->GetAABB();
			aabb1.MakeTransform(item1.transform);
			aabb2.MakeTransform(item2.transform);
			auto cernter1 = aabb1.min + (aabb1.max - aabb1.min) / 2.f;
			auto cernter2 = aabb2.min + (aabb2.max - aabb2.min) / 2.f;
			auto distance1 = glm::length2(state.camera.position - cernter1);
			auto distance2 = glm::length2(state.camera.position - cernter2);
			return distance1 > distance2;
		}
	);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	RenderHelp::renderTransparentScene(state, _shader, state.objects.sceneTransparentItems);
	glDisable(GL_BLEND);
}
