#include "ECS/Core/EntityManager.h"

EntityManager::EntityManager() {
	// 预分配所有实体ID
	for (EntityID id = 0; id < MAX_ENTITIES; ++id) {
		alive[id] = false;
		availableIds.push(id);
	}
}

EntityID EntityManager::createEntity() {
	if (availableIds.empty()) {
		throw std::runtime_error("Too many entities!");
	}

	EntityID id = availableIds.front();
	availableIds.pop();

	alive[id] = true;
	componentMasks[id].reset();
	livingEntityCount++;

	return id;
}

std::vector<EntityID> EntityManager::createMultiple(uint32_t count) {
	if (availableIds.size() <= count) {
		throw std::runtime_error("Too many entities!");
	}
	std::vector<EntityID> result;
	for (uint32_t i = 0; i < count; i++)
	{
		if (availableIds.empty())
			throw std::runtime_error("Too many entities!");

		EntityID id = availableIds.front();
		availableIds.pop();

		alive[id] = true;
		componentMasks[id].reset();
		livingEntityCount++;
		result.emplace_back(id);
	}
	return result;
}

void EntityManager::destroyEntity(EntityID id)
{
	if (!isValid(id)) return;

	alive[id] = false;
	componentMasks[id].reset();
	availableIds.push(id);
	livingEntityCount--;
}

void EntityManager::addComponent(EntityID id, ComponentTypeID componentTypeId)
{
	if (isValid(id)) {
		componentMasks[id].set(componentTypeId);
	}
}

void EntityManager::removeComponent(EntityID id, ComponentTypeID componentTypeId)
{
	if (isValid(id)) {
		componentMasks[id].reset(componentTypeId);
	}
}

ComponentMask EntityManager::getEntityComponentMask(EntityID id)
{
	return componentMasks[id];
}

void EntityManager::clearComponentMask(EntityID id) {
	if (isValid(id)) {
		componentMasks[id].reset();
	}
}

bool EntityManager::isValid(EntityID id) const {
	return id < MAX_ENTITIES && alive[id];
}

uint32_t EntityManager::getLivingEntityCount() const {
	return livingEntityCount;
}

std::vector<EntityID> EntityManager::getAllActiveEntities() const {
	std::vector<EntityID> result;
	result.reserve(livingEntityCount);

	for (EntityID id = 0; id < MAX_ENTITIES; ++id) {
		if (alive[id]) {
			result.push_back(id);
		}
	}
	return result;
}

void EntityManager::getAllActiveEntities(std::vector<EntityID>& out) const {
	out.clear();
	out.reserve(livingEntityCount);

	for (EntityID id = 0; id < MAX_ENTITIES; ++id) {
		if (alive[id]) {
			out.push_back(id);
		}
	}
}
