#include "ECSCore/SystemManager.h"
#include "ECSCore/World.h"
#include <algorithm>


SystemEntry::SystemEntry()
	: systemid(0), priority(0), enabled(false) {
}

SystemManager::SystemManager()
{
}

SystemManager::~SystemManager()
{
	clear();
}

void SystemManager::setWorld(World& world)
{
	m_world = &world;
	// 为所有系统设置World
	for (auto& entry : m_systems)
	{
		entry.system->onAttach(world);
	}
}

System* SystemManager::getSystemByName(const std::string& name) const {
	for (auto& entry : m_systems)
	{
		if (entry.name == name)
		{
			return entry.system.get();
		}
	}
	return nullptr;
}

void SystemManager::setSystemEnabled(const std::string& name, bool enabled)
{
	for (auto& entry : m_systems)
	{
		if (entry.name == name)
		{
			entry.enabled = enabled;
			return;
		}
	}
}

void SystemManager::setSystemPriority(const std::string& name, int priority)
{
	for (auto& entry : m_systems)
	{
		if (entry.name == name)
		{
			if (entry.priority != priority) {
				entry.priority = priority;
				m_needsSorting = true;
				return;
			}
		}
	}
}

void SystemManager::preUpdate(float deltaTime)
{
	if (m_needsSorting)
		sortSystems();

	for (auto& entry : m_systems)
	{
		if (entry.enabled)
		{
			entry.system->preUpdate(deltaTime);
		}
	}
}

void SystemManager::update(float deltaTime)
{
	if (m_needsSorting)
		sortSystems();

	for (auto& entry : m_systems)
	{
		if (entry.enabled)
		{
			entry.system->update(deltaTime);
		}
	}
}

void SystemManager::postUpdate(float deltaTime)
{
	if (m_needsSorting)
		sortSystems();

	for (auto& entry : m_systems)
	{
		if (entry.enabled)
		{
			entry.system->postUpdate(deltaTime);
		}
	}
}

void SystemManager::fixedUpdate(float fixedDeltaTime)
{
	if (m_needsSorting)
		sortSystems();

	for (auto& entry : m_systems)
	{
		if (entry.enabled)
		{
			entry.system->fixedUpdate(fixedDeltaTime);
		}
	}
}

void SystemManager::clear()
{
	// 通知所有系统它们将被移除
	for (auto& entry : m_systems) {
		entry.system->Detach();
	}
	m_systems.clear();
	m_typeIdToIndex.clear();
	m_needsSorting = false;
}

bool SystemManager::isSystemEnabled(const std::string& name) const
{
	for (auto& entry : m_systems)
	{
		if (entry.name == name)
			return entry.enabled;
	}
	throw("system not found!");
}

int SystemManager::getSystemPriority(const std::string& name) const
{
	for (auto& entry : m_systems)
	{
		if (entry.name == name)
			return entry.priority;
	}
	throw("system not found!");
}

std::vector<std::string> SystemManager::getSystemNames() const
{
	std::vector<std::string> names;
	names.reserve(m_systems.size());
	for (const auto& entry : m_systems) {
		names.push_back(entry.name);
	}
	return names;
}

size_t SystemManager::getSystemCount() const
{
	return m_systems.size();
}

size_t SystemManager::getEnabledSystemCount() const
{
	size_t count = 0;
	for (const auto& entry : m_systems)
	{
		if (entry.enabled)
		{
			count++;
		}
	}
	return count;
}

void SystemManager::sortSystems() {
	std::sort(m_systems.begin(), m_systems.end(),
		[](const SystemEntry& a, const SystemEntry& b) {
			return a.priority > b.priority;
		});
	m_needsSorting = false;

	// 更新类型索引映射
	m_typeIdToIndex.clear();
	for (size_t index = 0; index < m_systems.size(); ++index) {
		const auto& entry = m_systems[index];
		m_typeIdToIndex[entry.systemid] = index;
	}
}
