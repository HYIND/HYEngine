#pragma once
#include "vkstdafx.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

#include "ThreadPool.h"

#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------------------------
// 多帧流水线
//
// 一帧 = 一个帧任务，在帧线程池上依次跑完所有阶段（3D → 2D …），阶段之间在同一线程上串行。
// 帧与帧之间用 FlightSync 交错：每个阶段把自己的工作切成若干段，每段用 FlightSync::ProgressScope 包起来，
// 第 N+1 帧的第 k 段要等第 N 帧的第 k 段做完才开始。所以：
//   * 同一时间、同一段里最多只有一帧在跑，阶段内各帧共用的对象（Pass、实例缓冲……）不用加锁；
//   * 每帧进出的段数必须固定，和这一帧要不要真的画东西无关——不画的段也要进出一次（空段）。
//     段数对不上时流水线会在帧末尾把这帧的时间线直接放到很大兜底，不会卡死，但会失去流水，并打一次警告。
//
// 每帧有一张最终画面（RGBA8，渲染目标尺寸），各阶段依次往上画；画完的帧进完成队列，宿主用 WaitImage / FetchImage 取走。
//
// 目前一帧的段（共 10 段）：
//   段 1      资源回收（流水线自己的，VKContext::ProcessRetireAndPushFrameIndex）
//   段 2 ~ 9  VKRenderer3D：SetupRenderState / SetupIndirectDrawData / PreCalculate / Skinned / 预计算提交 /
//             清屏 / SceneRenderGraph / 后处理（Combin + Bloom + PostProcess，写最终画面）
//   段 10     VKRenderer2D：精灵 / 调试线叠到最终画面
// UI 不进流水线：宿主在 Render::Renderer::WaitImage 里取到最终画面后，在宿主线程上叠 UI、取事件。
// ---------------------------------------------------------------------------------------------------

// 多帧同步：每帧一条时间线，第 k 段做完就把自己的时间线推到 k；下一帧进第 k 段前等上一帧的时间线到 k
struct FlightSync
{
    std::shared_ptr<VKWrapper::VKTimelineSemaphore> preFrameTimeLine;
    std::shared_ptr<VKWrapper::VKTimelineSemaphore> curframeTimeLine;
    uint32_t curProgress = 0;
    int64_t* waitAccumUs = nullptr;		// 渲染性能统计：等上一帧放行的累计耗时（不统计时为空）

    void WaitForNextProgress();
    void SignalDoneProgress();

    // 一段工作的作用域：构造时等上一帧放行，析构时放行下一帧。
    // 用法：{ FlightSync::ProgressScope syncScope(sync); ... }
    struct ProgressScope
    {
        FlightSync& sync;
        explicit ProgressScope(FlightSync& sync) :sync(sync)
        {
            if (sync.waitAccumUs)
            {
                const int64_t start = RenderProfileNowUs();
                sync.WaitForNextProgress();
                *sync.waitAccumUs += RenderProfileNowUs() - start;
            }
            else
                sync.WaitForNextProgress();
        }
        ~ProgressScope() { sync.SignalDoneProgress(); }

        ProgressScope(const ProgressScope&) = delete;
        ProgressScope& operator=(const ProgressScope&) = delete;
    };
};

// 阶段挂在一帧上的私有数据（每帧一份）
struct FrameStageData
{
    virtual ~FrameStageData() = default;
};

// 提交给流水线的一帧：3D 的 RenderState，加上各阶段自己的输入（下标 = 阶段注册顺序，没有输入的阶段留空）
struct FrameRequest
{
    std::shared_ptr<RenderState> state;
    std::vector<std::shared_ptr<FrameStageData>> stageData;
};

struct FrameJob
{
    uint32_t frameIndex = 0;
    uint32_t slot = 0;                              // 最终画面的槽位：在途、完成队列、宿主手上的帧各不相同
    uint32_t flightIndex = 0;                       // 在途帧序号（0 ~ MaxFramesInFlight-1，同时在途的帧各不相同）：
                                                    // 阶段按它取自己的每帧中间目标
    std::shared_ptr<RenderState> state;
    std::shared_ptr<FlightSync> sync;
    std::shared_ptr<Texture2D> finalColor;          // 这一帧的最终画面
    std::vector<std::shared_ptr<FrameStageData>> stageData;   // 下标 = 阶段注册顺序；提交时带进来，阶段在 OnDispatch 里可以替换
    std::shared_ptr<ThreadPool::SubmitHandle<void>> handle;

    RenderFrameProfile* Profile() const { return state ? state->profile.get() : nullptr; }

    template<typename T>
    T* GetStageData(uint32_t stageIndex) const
    {
        return stageIndex < stageData.size() ? static_cast<T*>(stageData[stageIndex].get()) : nullptr;
    }
};

class IFrameStage
{
public:
    virtual ~IFrameStage() = default;

    // 渲染目标尺寸变了（第一次建也走这里）。调用时流水线已经停下、队列已清空。
    // flightCount：每帧中间目标要准备几份（按 FrameJob::flightIndex 取）
    virtual void OnResize(uint32_t width, uint32_t height, uint32_t flightCount) {}

    // 派发线程上每轮循环调一次（没有新帧时也调），用来做空闲清理
    virtual void OnTick() {}

    // 派发线程上串行调用：帧刚放进流水线、还没开始执行。
    // prev：上一个还在途的帧（没有就是空），用来接跨帧历史；跨帧的 CPU 状态（相机历史等）在这里推进最安全
    virtual void OnDispatch(FrameJob& job, const FrameJob* prev, uint32_t stageIndex) {}

    // 帧线程上调用。内部自己进出 FlightSync::ProgressScope，每帧进出的段数必须固定
    virtual void Execute(FrameJob& job, uint32_t stageIndex) = 0;
};

class VKFramePipeline
{
public:
    using ImageCallback = std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>;

    VKFramePipeline();
    ~VKFramePipeline();
    VKFramePipeline(const VKFramePipeline&) = delete;
    VKFramePipeline& operator=(const VKFramePipeline&) = delete;

    // 阶段按添加顺序执行。只能在流水线停着的时候加；尺寸已经确定的话会立刻对新阶段调一次 OnResize
    void AddStage(std::shared_ptr<IFrameStage> stage);
    uint32_t GetStageCount() const { return static_cast<uint32_t>(_stages.size()); }

    // 建 / 重建渲染目标：在跑的话先停，清空队列，重建最终画面，通知各阶段，再恢复原来的运行状态。
    // 尺寸没变直接返回 true
    bool Resize(uint32_t width, uint32_t height);
    uint32_t GetWidth() const { return _width; }
    uint32_t GetHeight() const { return _height; }
    bool IsTargetReady() const { return _width > 0 && _height > 0; }

    // 新帧放进候选队列；候选队列满了就挤掉最旧的（记一次丢帧）
    void PushFrame(FrameRequest request);
    // 候选队列里是否还有没被派发线程取走的帧；帧循环据此决定要不要重复提交旧数据
    bool HasPendingFrame();

    // 等一张画完的帧 / 有就取一张。帧在完成队列的锁内取出，回调在锁外执行，回调里可以做重活（宿主叠 UI 等）。
    // 回调拿到的最终画面一直归宿主用，直到下一次取到新帧：流水线分配槽位时会绕开它
    void WaitImage(const ImageCallback& callback);
    bool FetchImage(const ImageCallback& callback);

    void Run();
    void Stop();
    bool IsRunning() const { return !_stop; }

    uint32_t GetMaxFramesInFlight() const { return _maxFramesInFlight; }
    // 最终画面的份数：在途帧、完成队列各最多 MaxFramesInFlight 帧，再加宿主手上的 1 帧
    uint32_t GetSlotCount() const { return _maxFramesInFlight * 2 + 1; }

    // 渲染性能统计通道（编辑器「调试 → 渲染性能」）：不开启时各打点处只多一次判空
    const std::shared_ptr<RenderProfilerChannel>& GetRenderProfiler() const { return _renderProfiler; }

private:
    void ExecuteLoop();
    void ExecuteFrame(FrameJob& job);
    std::shared_ptr<FrameJob> MakeJob(FrameRequest request);
    void InitFinalColors();
    // 在完成队列的锁内取出一帧并记下宿主手上的槽位；wait 为 true 时没有就等
    std::shared_ptr<FrameJob> PopDoneFrame(bool wait);

private:
    std::vector<std::shared_ptr<IFrameStage>> _stages;

    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _maxFramesInFlight = 1;
    uint32_t _frameIndex = 0;
    std::vector<std::shared_ptr<Texture2D>> _finalColors;   // 每个槽位一张

    std::shared_ptr<RenderProfilerChannel> _renderProfiler = std::make_shared<RenderProfilerChannel>();

    std::queue<FrameRequest> _candidateFrames;
    SpinLock _candidateFramesMutex;

    SpinLock _executeMutex;
    ConditionVariable _executeCV;
    std::deque<std::shared_ptr<FrameJob>> _runningFrames;     // 只在派发线程上碰

    RecursiveSpinLock _doneFramesMutex;
    ConditionVariable _doneFramesCV;
    std::deque<std::shared_ptr<FrameJob>> _doneFrames;
    int64_t _hostSlot = -1;                                   // 宿主手上那帧的槽位（-1 = 没有），受 _doneFramesMutex 保护

    // 段数检查：记第一帧的段数，后面对不上就警告一次
    std::atomic<uint32_t> _segmentCount{ 0 };
    std::atomic<bool> _segmentWarned{ false };

    ThreadPool _frameTaskPool;
    std::atomic<bool> _stop{ true };
    std::shared_ptr<std::thread> _exeLoopThread;
};
