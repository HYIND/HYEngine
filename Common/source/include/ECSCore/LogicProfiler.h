#pragma once

// 逻辑性能统计：编辑器「调试 → 逻辑性能」窗口的数据来源。
// 每次 World::update 调用记一条 LogicFrameProfile（墙钟耗时，微秒）：
//   - ProcessCommands、各 System 的 preUpdate / update / postUpdate / fixedUpdate；
//   - RenderSystem 收集渲染数据的各段（线程池里并行执行）；
//   - 实体数和各系统上报的计数（粒子数等）。
// 逻辑按 World::m_logicDeltaTime 节拍更新，不是每次 update 调用都会跑 pre/update/post，
// 面板把「上一次逻辑更新之后到这一次逻辑更新」的所有调用合成一个逻辑 tick 来看。
//
// 不统计时零开销：窗口没打开时 LogicProfilerChannel::IsEnabled() 为 false，帧上不挂 profile，
// 各打点处只多一次判空，不分配内存、不取时间戳。

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline int64_t LogicProfileNowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

enum class LogicSystemPhase : uint32_t
{
	PreUpdate = 0,
	Update,
	PostUpdate,
	FixedUpdate,
	Count
};

inline const char* LogicSystemPhaseName(LogicSystemPhase phase)
{
	switch (phase)
	{
	case LogicSystemPhase::PreUpdate:	return "PreUpdate";
	case LogicSystemPhase::Update:		return "Update";
	case LogicSystemPhase::PostUpdate:	return "PostUpdate";
	case LogicSystemPhase::FixedUpdate:	return "FixedUpdate";
	default:							return "?";
	}
}

// RenderSystem::postUpdate 里提交给线程池的各段（并行执行）
enum class LogicRenderSection : uint32_t
{
	Model = 0,
	Light,
	Particle,
	LaserBeam,
	Skybox,
	DDGIVolume,
	SyncCamera,
	Sprite,
	GIFAnimation,
	DebugLines,
	UI,
	Count
};

inline const char* LogicRenderSectionName(LogicRenderSection section)
{
	switch (section)
	{
	case LogicRenderSection::Model:			return "ProcessModel";
	case LogicRenderSection::Light:			return "ProcessLight";
	case LogicRenderSection::Particle:		return "ProcessParticle";
	case LogicRenderSection::LaserBeam:		return "ProcessLaserBeam";
	case LogicRenderSection::Skybox:		return "ProcessSkybox";
	case LogicRenderSection::DDGIVolume:	return "ProcessDDGIVolume";
	case LogicRenderSection::SyncCamera:	return "SyncGLCamera";
	case LogicRenderSection::Sprite:		return "ProcessSprite (2D)";
	case LogicRenderSection::GIFAnimation:	return "ProcessGIFAnimation (2D)";
	case LogicRenderSection::DebugLines:	return "ProcessDebugLines (2D)";
	case LogicRenderSection::UI:			return "ProcessUI";
	default:								return "?";
	}
}

// System 的执行顺序（= 排序后的顺序）；只在注册 / 排序变化时重建，各帧共享
struct LogicSystemLayout
{
	int version = 0;
	std::vector<std::string> names;
	std::vector<int> priorities;
};

struct LogicSystemSample
{
	std::array<int64_t, size_t(LogicSystemPhase::Count)> us{};
	bool enabled = false;
};

struct LogicFrameProfile
{
	uint64_t frameIndex = 0;				// World::getFrameCount()
	int64_t startUs = 0;					// World::update 进入
	int64_t endUs = 0;						// World::update 返回
	int64_t commandsUs = 0;					// ProcessCommands
	float logicBudgetMs = 0.0f;				// 逻辑节拍（World::getLogicDeltaTime）
	float timeScale = 1.0f;

	bool logicUpdated = false;				// 这次调用跑了 pre/update/post
	int fixedSteps = 0;						// 这次调用跑了几次 fixedUpdate（含追帧的大步长）
	bool fixedCatchUp = false;				// 物理严重落后，跑了一次大步长追帧

	std::shared_ptr<const LogicSystemLayout> layout;
	std::vector<LogicSystemSample> systems;	// 下标同 layout->names

	// RenderSystem（只在逻辑更新的那次调用里有）
	bool renderCollected = false;
	int64_t renderCollectUs = 0;			// 提交各段到线程池 → 全部做完（墙钟）
	int64_t renderSubmitUs = 0;				// submitWriteBuffer 三缓冲交换
	std::array<std::atomic<int64_t>, size_t(LogicRenderSection::Count)> renderSections{};	// 由线程池各线程写
	bool submitted3D = false;
	bool submitted2D = false;
	bool submittedUI = false;

	// 规模
	uint32_t entityCount = 0;
	std::vector<std::pair<std::string, int64_t>> counters;	// 只在场景线程上写

	int64_t TotalUs() const { return endUs > startUs ? endUs - startUs : 0; }
	int64_t RenderSection(LogicRenderSection s) const { return renderSections[size_t(s)].load(std::memory_order_relaxed); }

	void SetCounter(const std::string& name, int64_t value)
	{
		for (auto& [key, v] : counters)
		{
			if (key == name)
			{
				v = value;
				return;
			}
		}
		counters.emplace_back(name, value);
	}
};

// profile 为空时什么都不做
struct LogicRenderSectionScope
{
	LogicFrameProfile* profile;
	LogicRenderSection section;
	int64_t start;

	LogicRenderSectionScope(LogicFrameProfile* p, LogicRenderSection s)
		: profile(p), section(s), start(p ? LogicProfileNowUs() : 0) {}
	~LogicRenderSectionScope()
	{
		if (profile)
			profile->renderSections[size_t(section)].fetch_add(LogicProfileNowUs() - start, std::memory_order_relaxed);
	}

	LogicRenderSectionScope(const LogicRenderSectionScope&) = delete;
	LogicRenderSectionScope& operator=(const LogicRenderSectionScope&) = delete;
};

// 编辑器 <-> World
class LogicProfilerChannel
{
public:
	bool IsEnabled() const { return _enabled.load(std::memory_order_relaxed); }

	// 从关到开时清空历史
	void SetEnabled(bool enabled)
	{
		const bool prev = _enabled.exchange(enabled, std::memory_order_relaxed);
		if (enabled && !prev)
			Clear();
	}

	void Clear()
	{
		std::lock_guard lock(_mutex);
		_history.clear();
	}

	void Publish(std::shared_ptr<const LogicFrameProfile> profile)
	{
		if (!profile || !IsEnabled())
			return;
		std::lock_guard lock(_mutex);
		_history.push_back(std::move(profile));
		while (_history.size() > _capacity)
			_history.pop_front();
	}

	std::vector<std::shared_ptr<const LogicFrameProfile>> Snapshot() const
	{
		std::lock_guard lock(_mutex);
		return { _history.begin(), _history.end() };
	}

private:
	std::atomic<bool> _enabled{ false };

	mutable std::mutex _mutex;
	std::deque<std::shared_ptr<const LogicFrameProfile>> _history;
	// 按 World::update 调用计；场景循环可能比逻辑节拍快好几倍，留够 300 个逻辑 tick
	size_t _capacity = 2400;
};
