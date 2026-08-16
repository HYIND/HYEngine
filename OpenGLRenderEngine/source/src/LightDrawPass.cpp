#include "OpenGLRenderEngine//RenderPass/LightDrawPass.h"
#include "glm/gtc/matrix_transform.hpp"

LightDrawPass::LightDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

void LightDrawPass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	for (auto& info : state.lights.dirLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, state.camera.position - light->getDirection() * state.camera.farPlane);
		model = glm::scale(model, glm::vec3(3.f * (state.camera.farPlane / 500.f)));
		_shader.setMat4("model", model);
		_shader.setVec3("lightColor", light->getColor());
		_shader.setFloat("Intensity", light->getIntensity());
		light->Draw(_shader);
	}

	for (auto& info : state.lights.pointLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, light->getPosition());
		model = glm::scale(model, glm::vec3(0.15f));
		_shader.setMat4("model", model);
		_shader.setVec3("lightColor", light->getColor());
		_shader.setFloat("Intensity", light->getIntensity());
		light->Draw(_shader);
	}
	for (auto& info : state.lights.spotLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, light->getPosition());
		//model = glm::scale(model, glm::vec3(0.15f));
		model = glm::scale(model, glm::vec3(0.02f));
		_shader.setMat4("model", model);
		_shader.setVec3("lightColor", light->getColor());
		_shader.setFloat("Intensity", light->getIntensity());
		light->Draw(_shader);
	}
}

bool LightDrawPass::ShouldExecute(RenderState& state) const { 
	return state.option.flags.lightDrawOn; 
}
