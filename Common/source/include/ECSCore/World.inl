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

	// 1. 添加组件数据
	T& component = m_componentManager->addComponent<T>(entity, std::forward<Args>(args)...);

	// 2. 更新实体掩码
	size_t typeId = ComponentType<T>::getId();
	m_entityManager->addComponent(entity.getId(), typeId);

	return component;
}

template<typename T>
void World::removeComponent(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		return;

	// 1. 移除组件数据
	m_componentManager->removeComponent<T>(entity);

	// 2. 更新实体掩码
	size_t typeId = ComponentType<T>::getId();
	m_entityManager->removeComponent(entity.getId(), typeId);
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

	return hasComponent(entity, ComponentType<T>::getId());
}

template<typename... Ts>
bool World::hasComponents(const Entity& entity) const {
	if (!IsValidWorldEntity(entity))
		return false;

	ComponentMask required = getComponentMask<Ts...>();
	ComponentMask actual = m_entityManager->getEntityComponentMask(entity.getId());
	return (actual & required) == required;
}

template<typename... Ts>
bool World::hasComponents(const EntityID& entityId) const {
	ComponentMask required = getComponentMask<Ts...>();
	ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
	return (actual & required) == required;
}

template<typename... Ts>
bool World::hasAnyComponent(const EntityID& entityId) const {
	ComponentMask required = getComponentMask<Ts...>();
	ComponentMask actual = m_entityManager->getEntityComponentMask(entityId);
	return (actual & required) != 0;  // 位与不为0表示至少有一个
}

// 获取拥有特定组件组合的所有Entity
template<typename... Ts>
std::vector<Entity> World::getEntitiesWith() {
	std::vector<Entity> result;
	std::vector<EntityID> allEntities;

	m_entityManager->getAllActiveEntities(allEntities);

	for (EntityID entityId : allEntities) {
		if (hasComponents<Ts...>(entityId)) {
			result.push_back(Entity(this, entityId));
		}
	}

	return result;
}

template<typename... Ts>
std::vector<Entity> World::getEntitiesWithAny() {
	std::vector<Entity> result;
	std::vector<EntityID> allEntities;

	m_entityManager->getAllActiveEntities(allEntities);

	for (EntityID entityId : allEntities) {
		if (hasAnyComponent<Ts...>(entityId)) {
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
	((mask.set(ComponentType<Ts>::getId())), ...);
	return mask;
}

#endif