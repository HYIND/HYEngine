#pragma once

#include "Entity.h"
#include "View.h"
#include "Types.h"
#include <vector>

//#include "EntityManager.h"
//#include "ComponentManager.h"
//#include "SystemManager.h"

#include "ECS/Core/EventSystem.h"

class Entity;
class System;

class EntityManager;
class ComponentManager;
class SystemManager;

class World {

public:
	World();
	~World();

	bool IsValidWorldEntity(Entity entity) const;

	// ==== Entity 创建 ====
	Entity createEntity();

	// 带标签创建（标签作为组件）
	template<typename TagType, typename... Args>
	Entity createEntityWithTag(Args&&... args);
	template<typename... TagTypes>
	Entity createEntityWithTags();

	std::vector<Entity> createEntities(uint32_t count);	// 批量创建

	// ==== Entity 销毁 ====
	void destroyEntity(const Entity& entity);

	void destroyEntityLater(const Entity& entity);

	template<typename EventType>
	void destroyEntityLaterWithEvent(const Entity& entity, const EventType& event);

	// ==== 组件操作 ====
	template<typename T, typename... Args>
	T& addComponent(const Entity& entity, Args&&... args);

	template<typename T>
	void removeComponent(const Entity& entity);

	template<typename T>
	T& getComponent(const Entity& entity);

	template<typename T>
	T* tryGetComponent(const Entity& entity);

	// ==== 查询 ====
	bool hasComponent(const Entity& entity, ComponentTypeID componentTypeId) const;

	template<typename T>
	bool hasComponent(const Entity& entity) const;

	template<typename... Ts>
	bool hasComponents(const Entity& entity) const;

	template<typename... Ts>
	bool hasComponents(const EntityID& entityId) const;

	template<typename... Ts>
	bool hasAnyComponent(const EntityID& entityId) const;

	// 获取拥有特定组件组合的所有Entity
	template<typename... Ts>
	std::vector<Entity> getEntitiesWith();

	// 获取指定组件中拥有任意一个指定组件的实体
	template<typename... Ts>
	std::vector<Entity> getEntitiesWithAny();

	template<typename... Ts>
	View<Ts...> getViewWith();

	// ==== System 操作 ====
	template<typename T, typename... Args>
	T& registerSystem(int priority = 0, Args&&... args);

	template<typename T>
	T* getSystem();

	void setSystemEnabled(const std::string& name, bool enabled);
	void setSystemPriority(const std::string& name, int priority);

	bool isSystemEnabled(const std::string& name) const;
	int getSystemPriority(const std::string& name) const;

	void update(float deltaTime);

	void start();
	void stop();
	bool isRunning() const;

	uint64_t getFrameCount() const;
	float getDeltaTime() const;
	float getLogicDeltaTime() const;
	void setLogicDeltaTime(float dt);
	float getFixedDeltaTime() const;
	void setFixedDeltaTime(float dt);

	// ==== 事件操作 ====

	// 订阅感兴趣事件
	template<typename System, typename Event>
	void Subscribe(std::function<void(const Event&)> callback);

	// 广播事件
	template<typename Event>
	void Emit(const Event& event);

	template<typename Event, typename... Args>
	void Emit(Args &&... args);

public:	//Camera
	void SetMainCamera(const Entity& entity);
	Entity GetMainCameraEntity() const;
	bool HasMainCameraEntity() const;

private:
	template<typename... Ts>
	ComponentMask getComponentMask() const;

private:
	EntityManager* m_entityManager;
	ComponentManager* m_componentManager;
	SystemManager* m_systemManager;

	std::unique_ptr<EventSystem> m_eventsystem;

	float m_logicDeltaTime = 1000.f / 60.0f; // 60Hz逻辑更新
	float m_logicAccumulator = 0.0f;

	float m_fixedDeltaTime = 1000.f / 60.0f; // 60Hz物理更新
	float m_fixedAccumulator = 0.0f;

	float m_deltaTime = 0.0f;
	uint64_t m_frameCount = 0;

	bool m_isRunning = false;

	Entity m_MainCameraEntity;
};

#include "ECS/Core/World.inl"
