#include "vkstdafx.h"
#include "RenderEngine/Renderer.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "RenderEngine/RendererUI.h"
#include "Render/Systems/RenderSystem.h"
#include "EngineSystemPriority.h"
#include "ECSCore/World.h"
#include "Helper/Tools.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKRenderer2D.h"
#include <glm/gtc/matrix_access.hpp>
#include <memory>
#include <algorithm>
#include <chrono>
#include <ranges>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace
{
    // 帧循环没事做时的短睡眠。
    // Windows 下 sleep_for 会按毫秒向上取整，进程没调 timeBeginPeriod 时还会被拉长到
    // 一个系统时钟周期（常见 15.6ms），足以让后端断粮；所以优先用高精度可等待计时器
    // （Win10 1803+），创建失败再退回 sleep_for。
    void FrameLoopShortSleep(std::chrono::microseconds duration)
    {
#ifdef _WIN32
        struct WaitableTimer
        {
            HANDLE handle = nullptr;
            WaitableTimer()
            {
                handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            }
            ~WaitableTimer()
            {
                if (handle)
                    CloseHandle(handle);
            }
        };
        thread_local WaitableTimer timer;
        if (timer.handle)
        {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(duration.count()) * 10; // 100ns 为单位，负数表示相对时间
            if (SetWaitableTimerEx(timer.handle, &due, 0, nullptr, nullptr, nullptr, 0))
            {
                WaitForSingleObject(timer.handle, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(duration);
    }

    constexpr std::chrono::microseconds kFrameLoopIdleSleep{ 200 };
}

using namespace Render;

Renderer::Renderer()
	:_renderer3D(std::make_unique<Renderer3D>())
	, _renderer2D(std::make_unique<Renderer2D>())
	, _uiRender(std::make_unique<RendererUI>())
	, _sceneFrames(std::make_shared<SceneFrameBuffer>())
{
	for (int index = 0; index < 3; ++index)
		_sceneFrames->setInitialValue(index, std::make_shared<RenderSceneFrameData>());
}

Render::Renderer::~Renderer()
{
	Shutdown();
}

void Renderer::SetVulkanRenderer(std::shared_ptr<VKRenderer3D> renderer)
{
	if (!_renderer3D)
		return;

	// 换后端：旧流水线先停掉，新后端挂到一条新流水线上（阶段顺序见 kStage3D / kStage2D）。
	// 帧循环不用停：它每轮都重新取流水线
	std::shared_ptr<VKFramePipeline> oldPipeline;
	{
		std::lock_guard lock(_mutex);
		oldPipeline = std::move(_pipeline);
		if (renderer)
		{
			_pipeline = std::make_shared<VKFramePipeline>();
			_pipeline->AddStage(renderer);
			_pipeline->AddStage(_renderer2D->GetBackend());
		}
	}
	if (oldPipeline)
		oldPipeline->Stop();

	_renderer3D->SetVulkanRenderer(std::move(renderer));
}

std::shared_ptr<VKFramePipeline> Renderer::GetFramePipeline() const
{
	std::lock_guard lock(_mutex);
	return _pipeline;
}

std::shared_ptr<RenderProfilerChannel> Renderer::GetRenderProfiler() const
{
	auto pipeline = GetFramePipeline();
	return pipeline ? pipeline->GetRenderProfiler() : nullptr;
}

bool Renderer::CreateRenderTarget(uint32_t width, uint32_t height)
{
	std::lock_guard lock(_lifecycleMutex);

	auto pipeline = GetFramePipeline();
	if (!pipeline || !VKCONTEXT->GetDevice())
		return false;

	width = std::max(1u, width);
	height = std::max(1u, height);

	if (pipeline->GetWidth() == width && pipeline->GetHeight() == height)
		return true;

	// 首个 target：宿主只建了设备，此时还没有 target，流水线也还没跑。
	if (!pipeline->IsTargetReady())
		return pipeline->Resize(width, height);

	return ResizeRenderTargetLocked(pipeline, width, height, {});
}

bool Renderer::ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
{
	std::lock_guard lock(_lifecycleMutex);

	auto pipeline = GetFramePipeline();
	if (!pipeline || !VKCONTEXT->GetDevice())
		return false;

	width = std::max(1u, width);
	height = std::max(1u, height);

	if (pipeline->GetWidth() == width && pipeline->GetHeight() == height)
		return false;

	if (!pipeline->IsTargetReady())
	{
		if (!pipeline->Resize(width, height))
			return false;
		if (beforeResize)
			beforeResize();
		return true;
	}

	return ResizeRenderTargetLocked(pipeline, width, height, beforeResize);
}

bool Renderer::ResizeRenderTargetLocked(const std::shared_ptr<VKFramePipeline>& pipeline,
	uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
{
	// 流水线拆除 target 之前，帧循环和派发线程都必须已经停下：否则仍可能有一帧被
	// 提交到已销毁的 target，而宿主侧的 vkDeviceWaitIdle 并不构成屏障。
	const bool wasRunning = !_stopFrameLoop.load();
	StopFrameLoopLocked();
	pipeline->Stop();

	if (beforeResize)
		beforeResize();

	// 流水线已经停了，这里的 Resize 只负责重建 target（最终画面 + 各阶段的中间目标）。
	pipeline->Resize(width, height);

	if (wasRunning)
		StartLocked();
	return true;
}

bool Renderer::Start()
{
	std::lock_guard lock(_lifecycleMutex);
	return StartLocked();
}

bool Renderer::StartLocked()
{
	auto pipeline = GetFramePipeline();
	if (!pipeline || !_sceneFrames)
		return false;

	pipeline->Run();
	if (!_stopFrameLoop.exchange(false))
		return true;
	_frameThread = std::thread(&Renderer::FrameLoop, this);
	return true;
}

void Renderer::StopFrameLoop()
{
	std::lock_guard lock(_lifecycleMutex);
	StopFrameLoopLocked();
}

void Renderer::StopFrameLoopLocked()
{
	_stopFrameLoop = true;
	if (!_frameThread.joinable())
		return;
	// 从帧循环线程内部调用（例如在帧循环里提交的任务）：循环会自行退出，
	// 这里再 join 就会死锁。
	if (_frameThread.get_id() == std::this_thread::get_id())
		return;
	_frameThread.join();
}

bool Renderer::IsFrameLoopRunning() const
{
	return !_stopFrameLoop.load();
}

void Renderer::Shutdown()
{
	{
		std::lock_guard lock(_lifecycleMutex);
		StopFrameLoopLocked();
		if (auto pipeline = GetFramePipeline())
			pipeline->Stop();
	}
	// 流水线停了以后才能释放 2D 后端：它是流水线的一个阶段
	if (_renderer2D)
		_renderer2D->Shutdown();
	if (_uiRender)
		_uiRender->Shutdown();
}

bool Renderer::HasRenderer() const
{
	return _renderer3D && _renderer3D->HasRenderer();
}

bool Renderer::IsRenderDeviceReady() const
{
	return HasRenderer() && VKCONTEXT->GetDevice() != nullptr;
}

bool Renderer::IsRenderTargetReady() const
{
	auto pipeline = GetFramePipeline();
	return pipeline && pipeline->IsTargetReady();
}

uint32_t Renderer::GetRenderTargetWidth() const
{
	auto pipeline = GetFramePipeline();
	return pipeline ? pipeline->GetWidth() : 0;
}

uint32_t Renderer::GetRenderTargetHeight() const
{
	auto pipeline = GetFramePipeline();
	return pipeline ? pipeline->GetHeight() : 0;
}

void Renderer::SetTargetFps(uint32_t fps) noexcept
{
	_targetFps.store(fps, std::memory_order_relaxed);
}

uint32_t Renderer::GetTargetFps() const noexcept
{
	return _targetFps.load(std::memory_order_relaxed);
}

void Renderer::FrameLoop()
{
	SetThreadDescription(GetCurrentThread(), L"Render::Renderer::FrameLoop");

	_fpsController.reset();
	_appliedFps = 0xFFFFFFFFu;
	if (!_lastSceneFrame)
		_lastSceneFrame = std::make_shared<RenderSceneFrameData>();

	while (!_stopFrameLoop)
	{
		// 节拍的读取与应用都在帧循环线程上：控制器只在这里被碰，
		// 宿主侧调用 SetTargetFps() 不会和 run() 竞争。
		const uint32_t targetFps = _targetFps.load(std::memory_order_relaxed);
		if (targetFps != _appliedFps)
		{
			if (targetFps != 0)
				_fpsController.setCurTargetFps(targetFps);
			_appliedFps = targetFps;
		}

		// 只有两种情况才提交：
		//   1. 场景线程提交了新数据——立即处理，放进流水线的候选队列（满了会挤掉最旧的一帧）；
		//   2. 没有新数据但候选队列已空——流水线马上要断粮，把上一帧数据再交一次，保持满载
		//      （TAA / DDGI 等跨帧累积的效果需要持续出帧才会收敛）。
		// 其余时候（无新数据且候选队列里还有帧）短睡眠后再看，避免空转重复解析同一帧。
		auto pipeline = GetFramePipeline();
		const bool hasNewData = _sceneFrames->hasNewData();
		if (!pipeline || (!hasNewData && pipeline->HasPendingFrame()))
		{
			FrameLoopShortSleep(kFrameLoopIdleSleep);
			continue;
		}

		if (hasNewData)
		{
			// 把读端那一格整个拿走，换一份空的进去：这一帧的数据要在流水线里排一两帧才画，
			// 不能留在三缓冲里等着被写端轮到、清空。写端只在写 / 就绪两格之间交换，碰不到读端这一格
			auto& readSlot = _sceneFrames->acquireReadBuffer();
			if (readSlot)
				_lastSceneFrame = std::exchange(readSlot, std::make_shared<RenderSceneFrameData>());
		}

		SubmitFrame(pipeline, _lastSceneFrame);

		// 0 means uncapped: skip the pacing sleep entirely.
		if (targetFps != 0)
			_fpsController.run();
	}
}

void Renderer::SubmitFrame(const std::shared_ptr<VKFramePipeline>& pipeline, const std::shared_ptr<RenderSceneFrameData>& frame)
{
	if (!pipeline || !frame || !_renderer3D)
		return;

	const auto& profiler = pipeline->GetRenderProfiler();
	FrameRequest request;
	request.state = _renderer3D->BuildRenderState(frame->scene3D, pipeline->GetWidth(), pipeline->GetHeight(),
		profiler && profiler->IsEnabled());
	if (!request.state)
		return;

	// 2D：和 3D 同一份帧数据，原样带给流水线里的 2D 阶段；层开关在这里记下
	auto input2D = std::make_shared<VKRenderer2D::FrameInput>();
	input2D->frame = frame->scene2D;
	input2D->visible = _layer2DVisible.load(std::memory_order_acquire);

	request.stageData.resize(kStageCount);
	request.stageData[kStage2D] = std::move(input2D);

	pipeline->PushFrame(std::move(request));
}

void Renderer::WaitImage(const WaitImageCallback& callback)
{
	auto pipeline = GetFramePipeline();
	if (!pipeline)
		return;

	// 流水线已经画完 3D + 2D；UI 留在宿主线程上，叠在最上面。
	// 回调在流水线的完成队列锁外执行，这一帧的最终画面在下一次取帧之前一直归这里用
	pipeline->WaitImage([this, &callback](std::shared_ptr<Texture2D> tex, std::shared_ptr<RenderState> state)
		{
			if (_uiRender && _layerUIVisible.load(std::memory_order_acquire))
				_uiRender->Render(tex);

			// 事件是 Render 里面（ImGui 控件判定时）产生的，所以只能在这里、同一线程紧接着取。
			if (_uiEventSink)
				_uiEventSink(_uiRender->PollEvents());

			if (callback)
				callback(tex, state);
		});
}

void Renderer::SetUiInputContext(std::shared_ptr<EngineInputContext> context)
{
	if (_uiRender)
		_uiRender->SetInputContext(std::move(context));
}

void Renderer::SetUiEventSink(std::function<void(std::vector<UI::UiEvent>)> sink)
{
	_uiEventSink = std::move(sink);
}

void Renderer::SetLayerVisibility(const LayerVisibility& visibility)
{
	if (_renderer3D)
		_renderer3D->SetSceneRenderEnabled(visibility.layer3D);
	_layer2DVisible.store(visibility.layer2D, std::memory_order_release);
	_layerUIVisible.store(visibility.layerUI, std::memory_order_release);
}

Renderer::LayerVisibility Renderer::GetLayerVisibility() const
{
	LayerVisibility visibility;
	visibility.layer3D = _renderer3D ? _renderer3D->IsSceneRenderEnabled() : true;
	visibility.layer2D = _layer2DVisible.load(std::memory_order_acquire);
	visibility.layerUI = _layerUIVisible.load(std::memory_order_acquire);
	return visibility;
}

void Renderer::BindRenderSystem(RenderSystem& renderSystem)
{
	renderSystem.SetTriBufferScene(_sceneFrames);
	renderSystem.SetTriBufferUI(_uiRender ? _uiRender->GetFrameBuffer() : nullptr);
}

void Renderer::RegisterRenderSystem(World& world)
{
	auto& renderSystem = world.registerSystem<RenderSystem>(ToSystemPriority(EngineSystemPriority::Render));
	BindRenderSystem(renderSystem);
}
