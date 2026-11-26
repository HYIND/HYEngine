#pragma once

// 渲染性能统计：编辑器「调试 → 渲染性能」窗口的数据来源。
//
// 两种时间：
//   * 墙钟时间（微秒）：单帧耗时、RenderGraph 之外的各步骤、SceneRenderGraph 整体、每个 Pass 的 FrameBegin / Execute。
//     RenderGraph 之外的步骤各自 SubmitNowAndWait，所以它们的墙钟时间已经包含 GPU 执行，就是这一步的完整耗时；
//     SceneRenderGraph 整体也一样（Execute 收尾会等图内全部 GPU 工作做完）。
//   * GPU 时间（时间戳）：只有 RenderGraph 里的 Pass 有。图内的提交接在同一条提交链上、CPU 不再逐个等 Pass 做完，
//     Pass 的 Execute 墙钟时间只是 CPU 侧的开销（取资源 + 录制 + 等待提交 + 提交），GPU 上花的时间要看 gpuUs。
//
// 不统计时零开销：窗口没打开时 RenderProfilerChannel::IsEnabled() 为 false，帧上不挂 RenderFrameProfile，
// 各打点处只多一次判空，不分配内存、不取时间戳、不建时间戳查询池。

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
	Queue,					// VKFramePipeline::PushFrame → 被派发线程取走、建成帧任务
	WaitPrevFrame,			// 帧任务里各段（FlightSyncScope）等上一帧放行的总和，不算进各步骤
	ProcessRetire,			// 帧开头的延迟资源回收（VKFramePipeline 的第 1 段）
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
	Finish,					// 3D 收尾：记下这一帧的 EV100（在后处理那一段里）
	Render2D,				// 2D 层叠到最终画面（VKRenderer2D，3D 之后的一段）
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
	case RenderProfileStep::Finish:					return "Finish";
	case RenderProfileStep::Render2D:				return "Render2D";
	default:										return "?";
	}
}

inline const char* RenderProfileStepTip(RenderProfileStep step)
{
	switch (step)
	{
	case RenderProfileStep::Analysis:		return "Renderer3D::ProcessFrame 里把场景帧数据整理成 RenderState（AnalysisRenderFrameData）";
	case RenderProfileStep::Queue:			return "从 PushFrame 到被流水线派发线程取走；在途帧满了时会变长";
	case RenderProfileStep::WaitPrevFrame:	return "多帧并行时，各段开始前等上一帧做完同一段的时间（FlightSync），不计入各步骤";
	case RenderProfileStep::ProcessRetire:	return "每帧第 1 段：VKContext::ProcessRetireAndPushFrameIndex，释放 GPU 已经用完的延迟资源";
	case RenderProfileStep::PreSubmit:		return "PreCalculate 与 Skinned 录完后的 SubmitNowAndWait，含 GPU 执行";
	case RenderProfileStep::ClearTargets:	return "清 sceneColorBuffer / firstPersonColorBuffer 并等待";
	case RenderProfileStep::SceneGraph:		return "SceneRenderGraph::Execute 整体（含等图内 GPU 工作做完），展开看每个 Pass 的 CPU / GPU 耗时";
	case RenderProfileStep::Render2D:		return "VKRenderer2D：精灵 / 调试线叠到最终画面，含上传实例和绘制两次 SubmitNowAndWait；没有 2D 内容或 2D 层关闭时是空段";
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
	// ── CPU（墙钟） ──
	int64_t frameBeginUs = 0;			// FrameBegin（各 Pass 并行执行；里面直接提交并等待的 GPU 工作会排在已提交的 Pass 之后，可能偏大）
	int64_t executeUs = 0;				// Execute：取资源 + 录制 + 等待提交 + 提交。有回读的 Pass（SubmitNowAndWait）还含等 GPU 把它和它之前的工作做完
	int64_t executeWaitUs = 0;			// 其中「等待提交」：等提交锁 —— 前面的 Pass 正在补差 / 提交，或有回读的 Pass 拿着锁在等 GPU
	bool executed = false;				// ShouldExecute 为 false 时不执行

	// ── GPU（时间戳） ──
	int64_t gpuUs = 0;					// 本 Pass 第一条命令开始到最后一条命令结束，含 Pass 内多次提交之间的空闲（如阴影图集按灯光分批提交、回读后的 CPU 处理）
	bool gpuValid = false;				// 有 GPU 计时：Pass 执行并提交过命令，且设备的图形队列支持时间戳

	// CPU 自身 = Execute - 等待提交
	int64_t SelfUs() const { return executeUs > executeWaitUs ? executeUs - executeWaitUs : 0; }
};

struct RenderFrameProfile
{
	uint32_t frameIndex = 0;
	int64_t pushTimeUs = 0;				// 进入候选队列（VKFramePipeline::PushFrame）的时刻
	int64_t startTimeUs = 0;			// 被派发线程取走、建成帧任务（VKFramePipeline::MakeJob）的时刻
	int64_t endTimeUs = 0;				// 画完、放进完成队列的时刻

	std::array<int64_t, size_t(RenderProfileStep::Count)> steps{};

	std::shared_ptr<const RenderGraphProfileLayout> graphLayout;
	std::vector<RenderPassProfile> passes;	// 每个槽位只由执行该 Pass 的线程写

	void Add(RenderProfileStep step, int64_t us) { steps[size_t(step)] += us; }
	int64_t Step(RenderProfileStep step) const { return steps[size_t(step)]; }
	// 单帧耗时：从被取走到画完（多帧并行时各帧会重叠）
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

	// 候选队列满时被新帧挤掉的帧数（在途帧已满、派发跟不上时出现）
	void AddDropped() { _dropped.fetch_add(1, std::memory_order_relaxed); }
	uint64_t Dropped() const { return _dropped.load(std::memory_order_relaxed); }

private:
	std::atomic<bool> _enabled{ false };
	std::atomic<uint64_t> _dropped{ 0 };

	mutable std::mutex _mutex;
	std::deque<std::shared_ptr<const RenderFrameProfile>> _history;
	size_t _capacity = 300;
};
