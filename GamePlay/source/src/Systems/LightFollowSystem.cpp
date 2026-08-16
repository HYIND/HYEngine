#include "Systems/LightFollowSystem.h"
#include "ECSCore/World.h"
#include "GamePlayComponents.h"
#include "CommonComponent.h"

void LightFollowSystem::postUpdate(float deltaTime)
{
	auto& world = getWorld();

	auto entities = world.getEntitiesWith<Transform, LightFollow>();
	for (auto entity : entities)
	{
		auto& lightFollow = entity.getComponent<LightFollow>();
		if (!lightFollow.target || !lightFollow.target.hasComponent<Transform>())
			continue;
		processLightFollow(entity, lightFollow.target);
	}
}

void LightFollowSystem::processLightFollow(Entity& lightEntity, Entity& targetEntity)
{
	auto& lightTrans = lightEntity.getComponent<Transform>();
	auto& lightFollow = lightEntity.getComponent<LightFollow>();

	auto& trans = targetEntity.getComponent<Transform>();

	lightTrans.position = trans.position;
	lightTrans.rotation = trans.rotation;
	lightTrans.scale = trans.scale;
}

