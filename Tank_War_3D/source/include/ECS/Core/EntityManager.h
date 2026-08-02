#pragma once

#include <array>
#include <unordered_map>
#include <vector>
#include <queue>
#include <bitset>
#include <string>
#include <stdexcept>
#include "Entity.h"

class EntityManager {

public:
	EntityManager();
	EntityID createEntity();
	std::vector<EntityID> createMultiple(uint32_t count);
	void destroyEntity(EntityID id);
	void addComponent(EntityID id, ComponentTypeID componentTypeId);
	void removeComponent(EntityID id, ComponentTypeID componentTypeId);
	void clearComponentMask(EntityID id);
	bool isValid(EntityID id) const;
	uint32_t getLivingEntityCount() const;
	std::vector<EntityID> getAllActiveEntities() const;
	void getAllActiveEntities(std::vector<EntityID>& out) const;
	ComponentMask getEntityComponentMask(EntityID id);

private:
	// 实体存储
	std::array<bool, MAX_ENTITIES> alive;					// 活跃实体标记
	std::array<ComponentMask, MAX_ENTITIES> componentMasks;	// 实体组件掩码
	std::queue<EntityID> availableIds;						// 可复用的实体ID队列
	uint32_t livingEntityCount = 0;							// 当前活跃实体数
};
