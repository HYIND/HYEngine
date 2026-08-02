#include "ECS/Systems/LightFollowSystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/AllComponent.h"

void LightFollowSystem::postUpdate(float deltaTime)
{
	auto& world = getWorld();

	auto entities = world.getEntitiesWith<Transform, LightFollow, RenderLight>();
	for (auto entity : entities)
	{
		auto& cameraFollow = entity.getComponent<LightFollow>();
		if (!cameraFollow.target || !cameraFollow.target.hasComponent<Transform>())
			continue;
		processLightFollow(entity, cameraFollow.target);
	}
}

void LightFollowSystem::processLightFollow(Entity& lightEntity, Entity& targetEntity)
{
	auto& lightTrans = lightEntity.getComponent<Transform>();
	auto& lightFollow = lightEntity.getComponent<LightFollow>();
	auto& lightComponent = lightEntity.getComponent<RenderLight>();

	auto& trans = targetEntity.getComponent<Transform>();

	lightTrans.position = trans.position;
	lightTrans.rotation = trans.rotation;
	lightTrans.scale = trans.scale;

	lightComponent.SetTransForm(lightTrans);
}

