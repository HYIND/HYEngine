#pragma once

#include "System.h"
#include <vector>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include "SystemTypes.h"

class System;
class World;

struct SystemEntry
{
	std::unique_ptr<System> system;
	int priority;      // 执行优先级
	bool enabled;      // 是否启用
	std::string name;  // 系统名称
	SystemTypeID systemid;

	SystemEntry();
};


class SystemManager
{

public:
	SystemManager();
	~SystemManager();

	template<typename T, typename... Args>
	T& registerSystem(int priority, Args&&... args);

	template<typename T>
	T* getSystem();

	void setWorld(World& world);
	void setSystemEnabled(const std::string& name, bool enabled);
	void setSystemPriority(const std::string& name, int priority);

	void preUpdate(float deltaTime);
	void update(float deltaTime);
	void postUpdate(float deltaTime);
	void fixedUpdate(float fixedDeltaTime);

	void clear();

	bool isSystemEnabled(const std::string& name) const;
	int getSystemPriority(const std::string& name) const;

	System* getSystemByName(const std::string& name) const;
	std::vector<std::string> getSystemNames() const;

	size_t getSystemCount() const;
	size_t getEnabledSystemCount() const;

private:
	void sortSystems();

private:
	std::vector<SystemEntry> m_systems;
	std::unordered_map<SystemTypeID, size_t> m_typeIdToIndex;
	World* m_world = nullptr;
	bool m_needsSorting = false;
};

#include "ECS/Core/SystemManager.inl"