#include "Systems/LightShowSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "CommonSystems.h"
#include <algorithm>

void LightShowSystem::update(float deltaTime)
{
	auto lightEntities = m_world->getEntitiesWith<TagLightShowLight, TagLight, Transform, RenderLight>();
	auto lightShowEntitiesView = m_world->getViewWith<TagLightShowEntity, LightShow, Transform, Physics, VariableMaterial, ModelProxy>();

	for (Entity entity : lightShowEntitiesView.getEntities())
	{
		auto& lightshow = entity.getComponent<LightShow>();

		bool isLighting = false;

		for (Entity& lightEntity : lightEntities)
		{
			if (isEntityLighting(entity, lightEntity))
			{
				isLighting = true;
				break;
			}
		}

		float exposureChange = -lightshow.lossRate;

		if (isLighting)
			exposureChange += 350.f;

		exposureChange *= (deltaTime / 1000.f);

		lightshow.exposure = std::clamp(lightshow.exposure + exposureChange, 0.f, lightshow.thresold);

		if (!lightshow.isExcited && lightshow.exposure == lightshow.thresold)
			lightshow.isExcited = true;
		else if (lightshow.isExcited && lightshow.exposure == 0.f)
			lightshow.isExcited = false;

		auto& material = entity.getComponent<VariableMaterial>();
		if (lightshow.isExcited)
		{
			material.SetAlpahMode(VariableMaterialData::AlphaMode::Blend);
			material.SetOpacity(std::clamp(lightshow.exposure / (lightshow.thresold * 0.8f), 0.f, 1.f));
		}
		else {
			material.SetAlpahMode(VariableMaterialData::AlphaMode::Opaque);
			material.SetOpacity(0.0f);
		}
	}
}

bool LightShowSystem::isEntityLighting(Entity& entity, Entity& lightEntity)
{
	auto physicsSystem = m_world->getSystem<PhysicsSystem>();
	if (!physicsSystem) return false;

	auto& modelProxy = entity.getComponent<ModelProxy>();
	if (!modelProxy.data) return false;

	auto& trans = entity.getComponent<Transform>();
	auto& physics = entity.getComponent<Physics>();

	ModelProxyData::AABB aabb = modelProxy.data->GetAABB();
	glm::vec3 center = aabb.min + (aabb.max - aabb.min) / 2.f;
	center = trans.getMatrix() * glm::vec4(center, 1.0f);

	auto& lightTrans = lightEntity.getComponent<Transform>();
	auto& light = lightEntity.getComponent<RenderLight>();
	if (light.type == LightType::Directional)
	{
		float virtual_distance = 800.f;
		glm::vec3 virtual_lightPos = center - virtual_distance * lightTrans.getDirection();
		auto res = physicsSystem->raycast(center, virtual_lightPos);
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
	else if (light.type == LightType::Point)
	{
		auto res = physicsSystem->raycast(center, lightTrans.position);
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
	else if (light.type == LightType::Spot)
	{
		auto data = light.GetData<SpotLightData>();
		if (!data) return false;

		float outerCutOffAngle = std::max(data->cutOffAngle, data->outercutOffAngle);
		float outerCutOff = glm::cos(glm::radians(outerCutOffAngle));
		glm::vec3 lightDir = glm::normalize(center - lightTrans.position);

		float theta = glm::dot(lightDir, lightTrans.getDirection());
		if (theta < outerCutOff)
			return false;

		auto res = physicsSystem->raycast(center, lightTrans.position);
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
}
