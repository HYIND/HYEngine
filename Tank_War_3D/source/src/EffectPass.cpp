#include "OpenGLRenderEngine/RenderPass/EffectPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"

#include "Manager/ResourceManager.h"

#include "glm/gtc/matrix_transform.hpp"

EffectPass::EffectPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

bool EffectPass::ShouldExecute(RenderState& state) const
{
	return !state.objects.effectItems.empty();
}

void EffectPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	for (auto& item : state.objects.effectItems)
	{
		if (!item)
			continue;

		_shader.setInt("effectType", int(item->effectType));
		if (item->effectType == EffectType::Particle)
			DrawParticle(std::static_pointer_cast<BaseParticleProperties>(item), state);
		else if (item->effectType == EffectType::LaserBeam)
			DrawLaserBeam(std::static_pointer_cast<LaserBeamProperties>(item), state);
	}
}

void EffectPass::DrawParticle(std::shared_ptr<BaseParticleProperties> baseProperties, RenderState& state)
{
	glm::vec3 size = baseProperties->scale / 2.f;
	glm::vec3 pos = baseProperties->position;
	_shader.setVec3("particleSize", size);
	_shader.setVec3("particlePos", pos);
	_shader.setInt("particleType", int(baseProperties->particleType));

	if (baseProperties->particleType == ParticleType::Color || baseProperties->particleType == ParticleType::Color_Texture)
	{
		glEnable(GL_BLEND);
		glDisable(GL_CULL_FACE);	// 渲染双面
		glDepthMask(GL_FALSE);		// 关闭深度写入，但保留深度测试
		if (baseProperties->particleType == ParticleType::Color)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		//glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
		else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		ParticleShape shape = ParticleShape::BillboardDisc;
		if (baseProperties->particleType == ParticleType::Color)
		{
			auto properties = std::static_pointer_cast<ColorParticleProperties>(baseProperties);
			_shader.setVec3("color_texture.basecolor", properties->baseColor);
			_shader.setFloat("color_texture.opacity", properties->opacity);
			shape = properties->shape;
		}
		else
		{
			auto properties = std::static_pointer_cast<TextureParticleProperties>(baseProperties);
			_shader.setVec3("color_texture.basecolor", properties->baseColor);
			_shader.setFloat("color_texture.opacity", properties->opacity);
			shape = properties->shape;

			bool textureEnable = properties->texture && !properties->texture->IsEmpty();
			_shader.setBool("color_texture.textureEnable", textureEnable);
			if (textureEnable)
			{
				properties->texture->Bind(15);
				_shader.setInt("color_texture.texture", 15);
			}
		}

		_shader.setInt("particleShape", (int)shape);
		if (shape == ParticleShape::BillboardDisc || shape == ParticleShape::BillboardSoftDisc || shape == ParticleShape::BillboardQuad)
		{
			glm::mat4 billboardTrans = glm::translate(glm::mat4(1.0f), baseProperties->position)
				* glm::toMat4(Tool::SafeQuatLookAt(state.camera.position - baseProperties->position) * Tool::getQuatFromRotate(90.f, glm::vec3(1, 0, 0)))
				* glm::scale(glm::mat4(1.0f), baseProperties->scale);
			_shader.setMat4("model", billboardTrans);
			RenderHelp::renderBillboardQuad(_shader);
		}
		else if (shape == ParticleShape::Sphere)
		{
			_shader.setMat4("model", baseProperties->transform);
			RenderHelp::renderSphere(_shader);
		}

		glDepthMask(GL_TRUE);		// 恢复
		glEnable(GL_CULL_FACE);		// 恢复
		glDisable(GL_BLEND);
	}
	else if (baseProperties->particleType == ParticleType::Model)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		_shader.setMat4("model", baseProperties->transform);

		auto properties = std::static_pointer_cast<ModelParticleProperties>(baseProperties);
		_shader.setVec3("color_texture.basecolor", properties->baseColor);
		_shader.setFloat("color_texture.opacity", properties->opacity);
		if (properties->model)
			properties->model->Draw(_shader);

		glDisable(GL_BLEND);
	}
}

void EffectPass::DrawLaserBeam(std::shared_ptr<LaserBeamProperties> properties, RenderState& state)
{
	if (glm::length2(properties->end - properties->start) == 0.f)
		return;

	glEnable(GL_BLEND);
	glDisable(GL_CULL_FACE);	// 渲染双面
	//glDepthMask(GL_FALSE);		// 关闭深度写入，但保留深度测试
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	//glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);

	_shader.setVec3("laser_beam.color", properties->color);
	_shader.setFloat("laser_beam.white_width", properties->white_width);
	_shader.setFloat("laser_beam.color_width", properties->color_width);

	glm::vec3 direction = glm::normalize(properties->end - properties->start);
	glm::vec3 mid = properties->start + (properties->end - properties->start) / 2.f;
	glm::vec3 scale = glm::vec3(properties->white_width + properties->color_width, glm::length(properties->end - properties->start), properties->white_width + properties->color_width);

	glm::vec3 defaultDir = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::vec3 dir2D = glm::normalize(glm::vec3(direction.x, 0.0f, direction.z));
	float angle = atan2(dir2D.z, dir2D.x);
	glm::quat rotation = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));

	//glm::quatLookAt(direction, state.camera.direction)
	{

		glm::mat4 billboardTrans = glm::translate(glm::mat4(1.0f), mid)
			* glm::toMat4(Tool::SafeQuatLookAt(direction) * Tool::getQuatFromRotate(90.f, glm::vec3(1, 0, 0)))
			* glm::scale(glm::mat4(1.0f), scale);
		_shader.setMat4("model", billboardTrans);
		RenderHelp::renderCylinder(_shader);
	}
	//{
	//	glm::mat4 billboardTrans = glm::translate(glm::mat4(1.0f), mid)
	//		* glm::toMat4(glm::quatLookAt(direction, glm::vec3(1, 0, 0)) * Tool::getQuatFromRotate(-90.f, glm::vec3(0, 1, 0)))
	//		* glm::scale(glm::mat4(1.0f), scale);
	//	_shader.setMat4("model", billboardTrans);
	//	RenderHelp::renderBillboardQuad(_shader);
	//}

	//glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
}


