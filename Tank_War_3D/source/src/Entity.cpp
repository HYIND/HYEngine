#include "ECS/Core/Entity.h"

// 只读访问
EntityID Entity::getId() const { return id; }

World* Entity::getWorld() const { return world; }

// 有效性检查
bool Entity::isValid() const {
	return id != INVALID_ID && world != nullptr;
}

Entity::operator bool() const { return isValid(); }

bool Entity::operator==(const Entity& other) const { return id == other.id && world == other.world; }

bool Entity::operator!=(const Entity& other) const { return !(*this == other); }
