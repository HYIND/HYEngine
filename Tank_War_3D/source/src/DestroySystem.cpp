#include "ECS/Systems/DestroySystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/Tags.h"

void DestroySystem::preUpdate(float deltaTime)
{
	processDestructions();
}

void DestroySystem::update(float deltaTime)
{
	processDestructions();
}

void DestroySystem::postUpdate(float deltaTime)
{
	processDestructions();
}

void DestroySystem::processDestructions()
{
	auto entitiesToDestroy = m_world->getEntitiesWith<TagDestroy>();
	for (Entity& entity : entitiesToDestroy)
	{
		m_world->destroyEntity(entity);
	}
	entitiesToDestroy.clear();
}
