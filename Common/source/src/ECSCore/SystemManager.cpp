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

void SystemManager::BeginProfileFrame(LogicFrameProfile* profile)
{
	m_profile = profile;
	if (!profile)
		return;

	if (m_needsSorting)
		sortSystems();

	if (!m_profileLayout || m_profileLayout->names.size() != m_systems.size())
	{
		auto layout = std::make_shared<LogicSystemLayout>();
		layout->version = ++m_profileLayoutVersion;
		layout->names.reserve(m_systems.size());
		layout->priorities.reserve(m_systems.size());
		for (const auto& entry : m_systems)
		{
			layout->names.push_back(entry.name);
			layout->priorities.push_back(entry.priority);
		}
		m_profileLayout = std::move(layout);
	}

	profile->layout = m_profileLayout;
	profile->systems.assign(m_systems.size(), LogicSystemSample{});
	for (size_t i = 0; i < m_systems.size(); ++i)
		profile->systems[i].enabled = m_systems[i].enabled;
}

void SystemManager::runPhase(LogicSystemPhase phase, float deltaTime, void (System::*fn)(float))
{
	if (m_needsSorting)
		sortSystems();

	// 统计中途有 System 注册 / 重新排序（布局被置空）时下标对不上，这一帧剩下的就不记了
	LogicFrameProfile* profile = m_profile;
	if (profile && (profile->layout != m_profileLayout || profile->systems.size() != m_systems.size()))
		profile = nullptr;

	// 按下标遍历：System 的 update 里注册新 System 会让 vector 扩容，范围 for 的迭代器会失效
	for (size_t i = 0; i < m_systems.size(); ++i)
	{
		auto& entry = m_systems[i];
		if (!entry.enabled)
			continue;

		if (!profile)
		{
			(entry.system.get()->*fn)(deltaTime);
			continue;
		}

		const int64_t start = LogicProfileNowUs();
		System* system = entry.system.get();
		(system->*fn)(deltaTime);
		if (i < profile->systems.size())
			profile->systems[i].us[size_t(phase)] += LogicProfileNowUs() - start;
	}
}

void SystemManager::preUpdate(float deltaTime)
{
	runPhase(LogicSystemPhase::PreUpdate, deltaTime, &System::preUpdate);
}

void SystemManager::update(float deltaTime)
{
	runPhase(LogicSystemPhase::Update, deltaTime, &System::update);
}

void SystemManager::postUpdate(float deltaTime)
{
	runPhase(LogicSystemPhase::PostUpdate, deltaTime, &System::postUpdate);
}

void SystemManager::fixedUpdate(float fixedDeltaTime)
{
	runPhase(LogicSystemPhase::FixedUpdate, fixedDeltaTime, &System::fixedUpdate);
}

void SystemManager::clear()
{
	// 通知所有系统它们将被移除
	for (auto& entry : m_systems) {
		entry.system->Detach();
	}
	m_systems.clear();
	m_typeIdToIndex.clear();
	m_typeIdentities.clear();
	m_needsSorting = false;
	m_profile = nullptr;
	m_profileLayout.reset();
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
	m_profileLayout.reset();

	// 更新类型索引映射
	m_typeIdToIndex.clear();
	for (size_t index = 0; index < m_systems.size(); ++index) {
		const auto& entry = m_systems[index];
		m_typeIdToIndex[entry.systemid] = index;
	}
}
