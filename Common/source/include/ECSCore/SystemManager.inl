#ifndef SYSTEMMANAGER_IMPL_H
#define SYSTEMMANAGER_IMPL_H

#include "ECSCore/SystemManager.h"
#include <type_traits>
#include <stdexcept>

template<typename T, typename... Args>
inline T& SystemManager::registerSystem(int priority, Args&&... args)
{
	SystemTypeID systemid = SystemType<T>::getID();
	const EcsTypeIdentity identity = GetEcsTypeIdentity<T>();
	const std::string& systemTypeName = identity.stableName;
	auto named = m_typeIdentities.find(systemid);
	if (named != m_typeIdentities.end() && (named->second.stableName != identity.stableName || named->second.nativeName != identity.nativeName))
		throw std::runtime_error("ECS system type identity collision: " + identity.stableName + " (" + identity.nativeName + ")");
	if (auto ptr = getSystem<T>())
		return *ptr;

	auto system = std::make_unique<T>(std::forward<Args>(args)...);
	T& ref = *system;

	if (system->getName().empty()) {
		system->setName(systemTypeName);
	}


	SystemEntry entry;
	entry.system = std::move(system);
	entry.priority = priority;
	entry.enabled = true;
	entry.name = entry.system->getName();
	entry.systemid = systemid;

	m_systems.push_back(std::move(entry));
	m_typeIdToIndex[systemid] = m_systems.size() - 1;
	m_typeIdentities[systemid] = identity;
	m_needsSorting = true;

	if (m_world) {
		ref.Attach(*m_world);
	}

	return ref;
}

template<typename T>
inline T* SystemManager::getSystem() {
	SystemTypeID systemid = SystemType<T>::getID();
	const EcsTypeIdentity identity = GetEcsTypeIdentity<T>();
	auto named = m_typeIdentities.find(systemid);
	if (named != m_typeIdentities.end() && (named->second.stableName != identity.stableName || named->second.nativeName != identity.nativeName))
		throw std::runtime_error("ECS system type identity collision: " + identity.stableName + " (" + identity.nativeName + ")");
	auto it = m_typeIdToIndex.find(systemid);
	if (it == m_typeIdToIndex.end())
		return nullptr;

	return static_cast<T*>(m_systems[it->second].system.get());
}

#endif
