#include "ECS/Core/World.h"

#include "ECS/Core/EntityManager.h"
#include "ECS/Core/ComponentManager.h"
#include "ECS/Core/SystemManager.h"

#include "ECS/Core/EventSystem.h"

World::World() {
	m_entityManager = new EntityManager();
	m_componentManager = new ComponentManager();
	m_systemManager = new SystemManager();

	m_systemManager->setWorld(*this);

	m_eventsystem = std::make_unique<EventSystem>();
}

World::~World() {
	if (m_entityManager)
		delete m_entityManager;
	if (m_componentManager)
		delete m_componentManager;
	if (m_systemManager)
		delete m_systemManager;
}

// 创建Entity
Entity World::createEntity() {
	return Entity(this, m_entityManager->createEntity());
}

// 批量创建
std::vector<Entity> World::createEntities(uint32_t count) {
	std::vector<Entity> entities;
	auto ids = m_entityManager->createMultiple(count);
	for (auto id : ids)
		entities.emplace_back(this, id);
	return entities;
}

// ==== Entity 销毁 ====

void World::destroyEntity(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		return;

	if (entity)
	{
		m_componentManager->entityDestroyed(entity); // 清理组件
		m_entityManager->destroyEntity(entity.getId()); // 销毁实体
	}
}

void World::destroyEntityLater(const Entity& entity) {
	if (!IsValidWorldEntity(entity))
		return;

	m_eventsystem->Emit<EntityDestroyedEvent>(entity);
	if (auto* destroysystem = getSystem<DestroySystem>())
	{
		if (!entity.hasComponent<TagDestroy>())
			entity.addComponent<TagDestroy>();
	}
	else
		destroyEntity(entity);
}

// ==== 查询 ====
bool World::hasComponent(const Entity& entity, ComponentTypeID componentTypeId) const {
	if (!IsValidWorldEntity(entity))
		return false;

	ComponentMask mask = m_entityManager->getEntityComponentMask(entity.getId());
	return mask.test(componentTypeId);
}

void World::setSystemEnabled(const std::string& name, bool enabled) {
	m_systemManager->setSystemEnabled(name, enabled);
}

void World::setSystemPriority(const std::string& name, int priority) {
	m_systemManager->setSystemPriority(name, priority);
}

bool World::isSystemEnabled(const std::string& name) const
{
	return m_systemManager->isSystemEnabled(name);
}
int World::getSystemPriority(const std::string& name) const
{
	return m_systemManager->getSystemPriority(name);
}

void World::update(float deltaTime)
{
	if (!m_isRunning)
		return;

	m_deltaTime = deltaTime;
	m_frameCount++;

	m_logicAccumulator += deltaTime;
	bool shouupdateLogic = m_logicAccumulator >= m_logicDeltaTime;
	if (shouupdateLogic)
	{
		m_systemManager->preUpdate(m_logicAccumulator);
		m_systemManager->update(m_logicAccumulator);
	}

	{
		// 固定时间步长更新
		const int MAX_ITERATIONS = 6;

		int iterations = 0;
		m_fixedAccumulator += deltaTime;
		while (m_fixedAccumulator >= m_fixedDeltaTime && iterations < MAX_ITERATIONS)
		{
			m_systemManager->fixedUpdate(m_fixedDeltaTime);
			m_fixedAccumulator -= m_fixedDeltaTime;
			iterations++;
		}
		//if (iterations >= MAX_ITERATIONS && m_fixedAccumulator > m_fixedDeltaTime)
		//{
		//	m_fixedAccumulator = m_fixedDeltaTime;
		//}

		if (iterations >= MAX_ITERATIONS &&
			m_fixedAccumulator > m_fixedDeltaTime * 3)
		{
			// 严重落后（>50ms），需要特殊处理
			//LOG_WARNING("Physics severely behind: {}ms", m_fixedAccumulator * 1000);

			// 方案A：丢弃部分累积（保持游戏运行）
			//m_fixedAccumulator = m_fixedDeltaTime;  // 保留一帧

			// 方案B：执行一次大步长更新（精度降低但能追赶）
			float largeStep = std::min(m_fixedAccumulator, m_fixedDeltaTime * 3);
			m_systemManager->fixedUpdate(largeStep);
			m_fixedAccumulator -= largeStep;
		}
	}

	if (shouupdateLogic)
	{
		m_systemManager->postUpdate(m_logicAccumulator);
		m_logicAccumulator = 0;
	}
}

void World::start() { m_isRunning = true; }

void World::stop() { m_isRunning = false; }

bool World::isRunning() const { return m_isRunning; }

uint64_t World::getFrameCount() const { return m_frameCount; }

float World::getDeltaTime() const { return m_deltaTime; }

float World::getLogicDeltaTime() const { return m_logicDeltaTime; }

void World::setLogicDeltaTime(float dt) { m_logicDeltaTime = dt; }

float World::getFixedDeltaTime() const { return m_fixedDeltaTime; }

void World::setFixedDeltaTime(float dt) { m_fixedDeltaTime = dt; }

bool World::IsValidWorldEntity(Entity entity) const
{
	return entity && entity.getWorld() == this;
}

void World::SetMainCamera(const Entity& entity)
{
	if (m_MainCameraEntity) {
		m_MainCameraEntity.removeComponent<TagMainCamera>();
	}
	if (entity.isValid())
	{
		if (!entity.hasComponent<TagMainCamera>())
			entity.addComponent<TagMainCamera>();
		m_MainCameraEntity = entity;
	}
	else
		m_MainCameraEntity = Entity();
}

Entity World::GetMainCameraEntity() const { return m_MainCameraEntity; }

bool World::HasMainCameraEntity() const { return m_MainCameraEntity.isValid(); }