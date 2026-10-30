#ifndef WORLD_IMPL_H
#define WORLD_IMPL_H

#include "World.h"
#include "ComponentTypes.h"
#include "EntityManager.h"
#include "ComponentManager.h"
#include "SystemManager.h"
#include <type_traits>
#include "CommonEventDef.h"
#include "CommonTags.h"
#include "Systems/DestroySystem.h"

template<typename TagType, typename... Args>
Entity World::createEntityWithTag(Args&&... args) {
	Entity entity = createEntity();
	if (IsValidWorldEntity(entity)) {
		addComponent<TagType>(entity, std::forward<Args>(args)...);
	}
	return entity;
}

template<typename... TagTypes>
Entity World::createEntityWithTags() {
	Entity entity = createEntity();
	if (IsValidWorldEntity(entity)) {
		(addComponent<TagTypes>(entity), ...);
	}
	return entity;
}

template<typename EventType>
void World::destroyEntityLaterWithEvent(const Entity& entity, const EventType& event)
{
	if (!IsValidWorldEntity(entity))
		return;

	m_eventsystem->Emit<EventType>(event);
	m_eventsystem->Emit<EntityDestroyedEvent>(entity);
	if (auto* destroysystem = getSystem<DestroySystem>())
	{
		if (!entity.hasComponent<TagDestroy>())
			entity.addComponent<TagDestroy>();
	}
	else
		destroyEntity(entity);
}

template<typename T, typename... Args>
T& World::addComponent(const Entity& entity, Args&&... args) {
	if (!IsValidWorldEntity(entity))
		throw("is not a invaild entity in this world");

	// 添加组件数据
	T& component = m_componentManager->addComponent<T>(entity, std::forward<Args>(args)...);

	// 更新实体掩码
	ComponentSlotID slot = m_componentManager->getComponentSlot<T>();
	m_entityManager->addComponent(entity.getId(), slot);

	return component;
}

template<typename T>
void World::removeComponent(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		return;

	// 1. 移除组件数据
	m_componentManager->removeComponent<T>(entity);

	// 2. 更新实体掩码
	ComponentSlotID slot = m_componentManager->getComponentSlot<T>();
	m_entityManager->removeComponent(entity.getId(), slot);
}


template<typename T>
T& World::getComponent(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		throw("is not a invaild entity in this world");

	return m_componentManager->getComponent<T>(entity.getId());
}

template<typename T>
T* World::tryGetComponent(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		return nullptr;

	return entity ? m_componentManager->tryGetComponent<T>(entity.getId()) : nullptr;
}

template<typename T>
bool World::hasComponent(const Entity& entity) const {
	if (!IsValidWorldEntity(entity))
		return false;

	ComponentSlotID slot = 0;
	if (!m_componentManager->tryGetComponentSlot(ComponentType<T>::getId(), slot))
		return false;
	return m_entityManager->getEntityComponentMask(entity.getId()).test(slot);
}

template<typename... Ts>
bool World::hasComponents(const Entity& entity) const {
	if (!IsValidWorldEntity(entity))
		return false;

	ComponentMask required;
	if (!tryGetComponentMask<Ts...>(required))
		return false;
	ComponentMask actual = m_entityManager->getEntityComponentMask(entity.getId());
	return (actual & required) == required;
}

template<typename... Ts>
bool World::hasComponents(const EntityID& entityId) const {
	ComponentMask required;
	if (!tryGetComponentMask<Ts...>(required))
		return false;
	ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
	return (actual & required) == required;
}

template<typename... Ts>
bool World::hasAnyComponent(const EntityID& entityId) const {
	ComponentMask required;
	if (!tryGetComponentMask<Ts...>(required))
		return false;
	ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
	return (actual & required) != 0;  // 位与不为0表示至少有一个
}

// 获取拥有特定组件组合的所有Entity
template<typename... Ts>
std::vector<Entity> World::getEntitiesWith() {
	std::vector<Entity> result;

	ComponentMask required;
	if (!tryGetComponentMask<Ts...>(required))
		return result;

	std::vector<EntityID> allEntities;
	m_entityManager->getAllActiveEntities(allEntities);
	result.reserve(allEntities.size());

	for (EntityID entityId : allEntities) {
		ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
		if ((actual & required) == required) {
			result.push_back(Entity(this, entityId));
		}
	}

	return result;
}

template<typename... Ts>
std::vector<Entity> World::getEntitiesWithAny() {
	std::vector<Entity> result;

	ComponentMask required;
	if (!tryGetComponentMask<Ts...>(required))
		return result;

	std::vector<EntityID> allEntities;
	m_entityManager->getAllActiveEntities(allEntities);
	result.reserve(allEntities.size());

	for (EntityID entityId : allEntities) {
		ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
		if ((actual & required) != 0) {
			result.push_back(Entity(this, entityId));
		}
	}

	return result;
}

template<typename... Ts>
View<Ts...>  World::getViewWith() {
	return View<Ts...>(getEntitiesWith<Ts...>());
}

template<typename T, typename... Args>
T& World::registerSystem(int priority, Args&&... args)
{
	T& system = m_systemManager->registerSystem<T>(priority, std::forward<Args>(args)...);
	system.setWorld(this);
	return system;
}

template<typename T>
T* World::getSystem() {
	return m_systemManager->getSystem<T>();
}


// 订阅感兴趣事件
template<typename System, typename Event>
void World::Subscribe(std::function<void(Event&)> callback)
{
	m_eventsystem->Subscribe<System>(callback);
}

// 广播事件
template<typename Event>
void World::Emit(Event& event)
{
	m_eventsystem->Emit<Event>(event);
}

template<typename Event>
void World::Emit(const Event& event)
{
	Event temp = event;
	m_eventsystem->Emit<Event>(temp);
}

template<typename Event, typename ...Args>
void World::Emit(Args && ...args)
{
	m_eventsystem->Emit<Event>(std::forward<Args>(args)...);
}

template<typename ...Ts>
ComponentMask World::getComponentMask() const {
	ComponentMask mask;
	((mask.set(m_componentManager->getComponentSlot<Ts>())), ...);
	return mask;
}

// 只读版本：绝不注册组件。有一个类型没注册过就返回 false（没有实体能匹配）。
template<typename ...Ts>
bool World::tryGetComponentMask(ComponentMask& out) const {
	bool allRegistered = true;
	ComponentMask mask;
	(
		[&]() {
			ComponentSlotID slot = 0;
			if (m_componentManager->tryGetComponentSlot(ComponentType<Ts>::getId(), slot))
				mask.set(slot);
			else
				allRegistered = false;
		}(), ...);
	out = mask;
	return allRegistered;
}

#endif
