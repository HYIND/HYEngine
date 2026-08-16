#include "Systems/DestroySystem.h"
#include "ECSCore/World.h"
#include "CommonTags.h"

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
