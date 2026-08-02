#ifndef SYSTEMMANAGER_IMPL_H
#define SYSTEMMANAGER_IMPL_H

#include "ECS/Core/SystemManager.h"
#include <type_traits>

template<typename T, typename... Args>
inline T& SystemManager::registerSystem(int priority, Args&&... args)
{
	SystemTypeID systemid = SystemType<T>::getID();

	assert(m_typeIdToIndex.find(systemid) == m_typeIdToIndex.end());

	auto system = std::make_unique<T>(std::forward<Args>(args)...);
	T& ref = *system;

	if (system->getName().empty()) {
		system->setName(typeid(T).name());
	}


	SystemEntry entry;
	entry.system = std::move(system);
	entry.priority = priority;
	entry.enabled = true;
	entry.name = entry.system->getName();
	entry.systemid = systemid;

	m_systems.push_back(std::move(entry));
	m_typeIdToIndex[systemid] = m_systems.size() - 1;
	m_needsSorting = true;

	if (m_world) {
		ref.Attach(*m_world);
	}

	return ref;
}

template<typename T>
inline T* SystemManager::getSystem() {
	SystemTypeID systemid = SystemType<T>::getID();
	auto it = m_typeIdToIndex.find(systemid);
	if (it == m_typeIdToIndex.end())
		return nullptr;

	return static_cast<T*>(m_systems[it->second].system.get());
}

#endif