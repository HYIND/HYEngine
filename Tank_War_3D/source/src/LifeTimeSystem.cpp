#include "ECS/Systems/LifeTimeSystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/AllComponent.h"

void LifetimeSystem::update(float dt)
{
}

void LifetimeSystem::preUpdate(float dt)
{
	std::vector<Entity> entities = m_world->getEntitiesWith<LifeTime>();
	for (auto& entity : entities)
	{
		auto& lifetime = entity.getComponent<LifeTime>();
		lifetime.update(dt);

		if (lifetime.remainingTime < 0.f && lifetime.autoDestroy)
		{
			entity.addComponent<TagLifeTimeOut>();
			m_world->destroyEntityLater(entity);
		}
	}
}