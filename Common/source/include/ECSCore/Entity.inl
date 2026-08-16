#ifndef ENTITY_IMPL_H
#define ENTITY_IMPL_H

class World;

#include "Entity.h"
//#include "World.h"
#include <type_traits>
#include <stdexcept>

template<typename T, typename... Args>
inline T& Entity::addComponent(Args&&... args) const {
	if (!isValid())
		throw std::runtime_error("Entity is not valid");
	return getWorld()->template addComponent<T>(*this, std::forward<Args>(args)...);
}

template<typename T>
inline void Entity::removeComponent() const {
	if (!isValid())
		return;
	getWorld()->template removeComponent<T>(*this);
}

template<typename T>
inline T& Entity::getComponent() const {
	if (!isValid())
		throw std::runtime_error("Entity is not valid");
	return getWorld()->template getComponent<T>(*this);
}

template<typename T>
inline T* Entity::tryGetComponent() const {
	if (!isValid())
		return nullptr;
	return getWorld()->template tryGetComponent<T>(*this);
}

template<typename T>
inline bool Entity::hasComponent() const {
	if (!isValid())
		return false;
	return getWorld()->template hasComponent<T>(*this);
}

template<typename... Ts>
inline bool Entity::hasComponents() const {
	if (!isValid())
		return false;
	return getWorld()->template hasComponents<Ts...>(*this);
}

#endif