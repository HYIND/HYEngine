#include "ECS/Systems/LightShowSystem.h"
#include "ECS/Components/AllComponent.h"
#include "ECS/Systems/PhysicsSystem.h"
#include "ECS/Core/World.h"

void LightShowSystem::update(float deltaTime)
{
	auto lightEntities = m_world->getEntitiesWith<TagLight, RenderLight, TagLightShowLight>();
	auto lightShowEntities = m_world->getEntitiesWith<LightShow, RenderModel, Transform, Physics>();
	for (Entity& entity : lightShowEntities)
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

		auto& rendermodel = entity.getComponent<RenderModel>();
		if (rendermodel.model)
		{
			for (auto& meshinfo : rendermodel.model->getMeshInfos())
			{
				if (!meshinfo.material)
					continue;

				if (lightshow.isExcited)
					meshinfo.material->SetOpacity(std::clamp(lightshow.exposure / (lightshow.thresold * 0.8f), 0.f, 1.f));
				else
					meshinfo.material->SetOpacity(0.0f);
			}
		}
	}
}

bool LightShowSystem::isEntityLighting(Entity& entity, Entity& lightEntity)
{
	auto physicsSystem = m_world->getSystem<PhysicsSystem>();
	if (!physicsSystem) return false;

	auto& rendermodel = entity.getComponent<RenderModel>();
	if (!rendermodel.model) return false;

	auto& trans = entity.getComponent<Transform>();
	auto& physics = entity.getComponent<Physics>();

	AABB aabb = rendermodel.model->GetAABB();
	glm::vec3 center = aabb.min + (aabb.max - aabb.min) / 2.f;
	center = trans.getMatrix() * glm::vec4(center, 1.0f);

	auto& renderlight = lightEntity.getComponent<RenderLight>();
	if (renderlight.type == LightType::Dir && renderlight.dirlight)
	{
		auto& light = renderlight.dirlight;
		float virtual_distance = 800.f;
		glm::vec3 virtual_lightPos = center - virtual_distance * light->getDirection();
		auto res = physicsSystem->raycast(center, virtual_lightPos);
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
	else if (renderlight.type == LightType::Point && renderlight.pointlight)
	{
		auto& light = renderlight.pointlight;
		auto res = physicsSystem->raycast(center, light->getPosition());
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
	else if (renderlight.type == LightType::Spot && renderlight.spotlight)
	{
		auto& light = renderlight.spotlight;

		float outerCutOffAngle = std::max(light->getCutOffAngle(), light->getOuterCutOffAngle());
		float outerCutOff = glm::cos(glm::radians(outerCutOffAngle));
		glm::vec3 lightDir = normalize(center - light->getPosition());

		float theta = glm::dot(lightDir, light->getDirection());
		if (theta < outerCutOff)
			return false;

		auto res = physicsSystem->raycast(center, light->getPosition());
		if (res.hitEntity == entity)
			return true;
		return !res.hit;
	}
}
