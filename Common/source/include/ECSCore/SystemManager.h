#pragma once

#include "System.h"
#include <vector>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include "SystemTypes.h"
#include "ECSCore/LogicProfiler.h"

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

	// 逻辑性能统计：World::update 开头传入这一帧的 profile（结尾传 nullptr）。
	// 这里先把排序做掉，保证这一帧内各阶段的 System 顺序和 profile->systems 下标一致。
	void BeginProfileFrame(LogicFrameProfile* profile);

private:
	void sortSystems();
	void runPhase(LogicSystemPhase phase, float deltaTime, void (System::*fn)(float));

private:
	std::vector<SystemEntry> m_systems;
	std::unordered_map<SystemTypeID, size_t> m_typeIdToIndex;
	std::unordered_map<SystemTypeID, EcsTypeIdentity> m_typeIdentities;
	World* m_world = nullptr;
	bool m_needsSorting = false;

	LogicFrameProfile* m_profile = nullptr;
	std::shared_ptr<const LogicSystemLayout> m_profileLayout;	// 注册 / 排序后置空，下次统计时重建
	int m_profileLayoutVersion = 0;
};

#include "ECSCore/SystemManager.inl"
