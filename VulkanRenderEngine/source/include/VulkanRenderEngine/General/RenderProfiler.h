#pragma once

// 渲染性能统计：编辑器「调试 → 渲染性能」窗口的数据来源。
// 只记墙钟耗时（微秒），每一步就是「这一步实际花了多久」，不区分 CPU / GPU：
// RenderGraph 里每个 Pass 结束时都会等自己提交的 GPU 工作做完，所以 Pass 的 Execute 耗时已经包含 GPU 执行。
//
// 不统计时零开销：窗口没打开时 RenderProfilerChannel::IsEnabled() 为 false，帧上不挂 RenderFrameProfile，
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

inline int64_t RenderProfileNowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 一帧里 RenderGraph 之外的步骤（按执行顺序）
enum class RenderProfileStep : uint32_t
{
	Analysis = 0,			// Renderer3D：帧数据解析（AnalysisRenderFrameData）
	Queue,					// PushFrameState → 被 ExecuteLoop 取走
	WaitPrevFrame,			// DrawOffScreen 各段等上一帧放行（FlightSync）的总和，不算进各步骤
	ProcessRetire,
	SetupRenderState,
	SetupIndirectDrawData,
	PreCalculate,
	Skinned,
	PreSubmit,				// 预计算 + 蒙皮的提交与等待
	ClearTargets,
	SceneGraph,				// SceneRenderGraph 整体
	Combin,
	Bloom,
	PostProcess,
	Finish,
	Present,
	Count
};

inline const char* RenderProfileStepName(RenderProfileStep step)
{
	switch (step)
	{
	case RenderProfileStep::Analysis:				return "帧数据解析";
	case RenderProfileStep::Queue:					return "排队";
	case RenderProfileStep::WaitPrevFrame:			return "等待上一帧";
	case RenderProfileStep::ProcessRetire:			return "ProcessRetire";
	case RenderProfileStep::SetupRenderState:		return "SetupRenderState";
	case RenderProfileStep::SetupIndirectDrawData:	return "SetupIndirectDrawData";
	case RenderProfileStep::PreCalculate:			return "PreCalculate";
	case RenderProfileStep::Skinned:				return "Skinned";
	case RenderProfileStep::PreSubmit:				return "预计算+蒙皮 提交";
	case RenderProfileStep::ClearTargets:			return "清屏";
	case RenderProfileStep::SceneGraph:				return "SceneRenderGraph";
	case RenderProfileStep::Combin:					return "Combin";
	case RenderProfileStep::Bloom:					return "Bloom";
	case RenderProfileStep::PostProcess:			return "PostProcess";
	case RenderProfileStep::Finish:					return "FinishRendering";
	case RenderProfileStep::Present:				return "Present";
	default:										return "?";
	}
}

inline const char* RenderProfileStepTip(RenderProfileStep step)
{
	switch (step)
	{
	case RenderProfileStep::Analysis:		return "Renderer3D::ProcessFrame 里把场景帧数据整理成 RenderState（AnalysisRenderFrameData）";
	case RenderProfileStep::Queue:			return "从 PushFrameState 到被渲染循环取走；在途帧满了时会变长";
	case RenderProfileStep::WaitPrevFrame:	return "多帧并行时，各段开始前等上一帧做完同一段的时间（FlightSync），不计入各步骤";
	case RenderProfileStep::PreSubmit:		return "PreCalculate 与 Skinned 录完后的 SubmitNowAndWait，含 GPU 执行";
	case RenderProfileStep::ClearTargets:	return "清 sceneColorBuffer / firstPersonColorBuffer 并等待";
	case RenderProfileStep::SceneGraph:		return "SceneRenderGraph::Execute 整体，展开看每个 Pass";
	case RenderProfileStep::Present:		return "窗口模式下的 Present（离屏模式为 0）";
	default:								return nullptr;
	}
}

// RenderGraph 编译后的结构，给节点图用；只在 Compile 时重建一次，各帧共享
struct RenderGraphProfileLayout
{
	struct Node
	{
		std::string name;
		int batch = -1;
		bool fence = false;				// 栅栏节点（没有 RenderPass）
		std::vector<uint32_t> afters;	// 直接前驱在 nodes 里的下标（含冗余依赖，画图时自己做传递约简）
	};

	std::string graphName;
	int version = 0;
	std::vector<Node> nodes;			// 排序后的顺序（= 调度顺序），下标同 RenderFrameProfile::passes
};

struct RenderPassProfile
{
	int64_t frameBeginUs = 0;			// FrameBegin（各 Pass 并行执行）
	int64_t executeUs = 0;				// 取资源 + 录制 + 等待执行 + 提交并等 GPU 做完
	int64_t executeWaitUs = 0;			// 其中「等待执行」：录完后等前面的 Pass 在 GPU 上执行完、轮到自己（同一张图串行执行）
	bool executed = false;				// ShouldExecute 为 false 时不执行

	int64_t SelfUs() const { return executeUs > executeWaitUs ? executeUs - executeWaitUs : 0; }
};

struct RenderFrameProfile
{
	uint32_t frameIndex = 0;
	int64_t pushTimeUs = 0;				// 进入队列（PushFrameState）的时刻
	int64_t startTimeUs = 0;			// 被 ExecuteLoop 取走的时刻
	int64_t endTimeUs = 0;				// Present 完成的时刻

	std::array<int64_t, size_t(RenderProfileStep::Count)> steps{};

	std::shared_ptr<const RenderGraphProfileLayout> graphLayout;
	std::vector<RenderPassProfile> passes;	// 每个槽位只由执行该 Pass 的线程写

	void Add(RenderProfileStep step, int64_t us) { steps[size_t(step)] += us; }
	int64_t Step(RenderProfileStep step) const { return steps[size_t(step)]; }
	// 单帧耗时：从被取走到 Present 完成（多帧并行时各帧会重叠）
	int64_t TotalUs() const { return endTimeUs > startTimeUs ? endTimeUs - startTimeUs : 0; }
};

// profile 为空时什么都不做
struct RenderProfileScope
{
	RenderFrameProfile* profile;
	RenderProfileStep step;
	int64_t start;

	RenderProfileScope(RenderFrameProfile* p, RenderProfileStep s)
		: profile(p), step(s), start(p ? RenderProfileNowUs() : 0) {}
	~RenderProfileScope()
	{
		if (profile)
			profile->Add(step, RenderProfileNowUs() - start);
	}

	RenderProfileScope(const RenderProfileScope&) = delete;
	RenderProfileScope& operator=(const RenderProfileScope&) = delete;
};

// 编辑器 <-> 渲染器
class RenderProfilerChannel
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
		_dropped.store(0, std::memory_order_relaxed);
	}

	void SetCapacity(size_t capacity)
	{
		std::lock_guard lock(_mutex);
		_capacity = std::max<size_t>(1, capacity);
		while (_history.size() > _capacity)
			_history.pop_front();
	}

	void Publish(std::shared_ptr<const RenderFrameProfile> profile)
	{
		if (!profile || !IsEnabled())
			return;
		std::lock_guard lock(_mutex);
		_history.push_back(std::move(profile));
		while (_history.size() > _capacity)
			_history.pop_front();
	}

	std::vector<std::shared_ptr<const RenderFrameProfile>> Snapshot() const
	{
		std::lock_guard lock(_mutex);
		return { _history.begin(), _history.end() };
	}

	// 在途帧满时被新帧挤掉的帧数
	void AddDropped() { _dropped.fetch_add(1, std::memory_order_relaxed); }
	uint64_t Dropped() const { return _dropped.load(std::memory_order_relaxed); }

private:
	std::atomic<bool> _enabled{ false };
	std::atomic<uint64_t> _dropped{ 0 };

	mutable std::mutex _mutex;
	std::deque<std::shared_ptr<const RenderFrameProfile>> _history;
	size_t _capacity = 300;
};
