#pragma once

#include <limits>
#include <type_traits>
#include "Types.h"
#include <functional>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

class World;

class Entity {
public:
	explicit constexpr Entity(World* world = nullptr, EntityID id = INVALID_ID) :world(world), id(id) {}

	// 只读访问
	EntityID getId() const;
	World* getWorld() const;

	// 有效性检查
	bool isValid() const;

	explicit operator bool() const;

	// 比较操作
	bool operator==(const Entity& other) const;
	bool operator!=(const Entity& other) const;

	template<typename T, typename... Args>
	T& addComponent(Args&&... args) const;

	template<typename T>
	void removeComponent() const;

	template<typename T>
	T& getComponent() const;

	template<typename T>
	T* tryGetComponent() const;

	template<typename T>
	bool hasComponent() const;

	template<typename... Ts>
	bool hasComponents() const;

private:
	static constexpr EntityID INVALID_ID = std::numeric_limits<EntityID>::max();

private:
	EntityID id;
	mutable World* world;
};

#include "ECS/Core/Entity.inl"
