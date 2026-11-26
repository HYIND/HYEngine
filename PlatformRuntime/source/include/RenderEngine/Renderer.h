#pragma once

#include "RenderEngine/RenderFrameData.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "RenderEngine/RendererUI.h"

#include "Helper/DynamicFpsController.h"
#include "Helper/TripleBuffer.h"
#include "VulkanRenderEngine/VKRenderer3D.h"
#include "VulkanRenderEngine/VKFramePipeline.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Texture2D;
struct RenderState;
class World;
class RenderSystem;

namespace Render
{
	// 渲染门面：宿主只面对这一层。
	//
	// 一帧怎么走：
	//   场景线程   RenderSystem 把 3D + 2D 写进同一份 RenderSceneFrameData（一个三缓冲），UI 写进 RendererUI 的三缓冲
	//   帧循环线程 （这里）取走一帧 3D + 2D → 3D 整理成 RenderState、2D 原样带上 → 提交给帧流水线
	//   帧流水线   段 1 资源回收 → 3D（8 段）→ 2D（1 段），多帧之间按 FlightSync 交错 → 完成队列
	//   宿主线程   WaitImage 取到最终画面 → 叠 UI、取 UI 事件 → 宿主回调
	// UI 不进流水线：事件要在画 UI 的同一线程上紧接着取，而且 UI 要用最新的数据。
	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		// 三个子服务，只在需要具体类型时才取（例如宿主取相机快照）。
		Renderer3D* GetRenderer3D() noexcept { return _renderer3D.get(); }
		const Renderer3D* GetRenderer3D() const noexcept { return _renderer3D.get(); }
		Renderer2D* GetRenderer2D() noexcept { return _renderer2D.get(); }
		const Renderer2D* GetRenderer2D() const noexcept { return _renderer2D.get(); }

		RendererUI* GetRendererUI() noexcept { return _uiRender.get(); }
		const RendererUI* GetRendererUI() const noexcept { return _uiRender.get(); }

		// UI 的输入源：宿主那边填的 EngineInputContext，引擎 UI 每帧读一次做拾取。
		void SetUiInputContext(std::shared_ptr<EngineInputContext> context);
		// UI 交互事件的出口。宿主线程在 WaitImage 里画完 UI、取走这一帧的事件后调它，
		// 所以回调里只能入队，不能派发（派发要 World，必须在场景线程上）。
		// 传空表示不取事件（事件会攒在 RendererUI 里，直到有人来取）。
		void SetUiEventSink(std::function<void(std::vector<UI::UiEvent>)> sink);

		// 3D 后端：设进来时新建一条帧流水线，阶段依次是 3D、2D。实例和设备已经在 VKContext 里建好。
		void SetVulkanRenderer(std::shared_ptr<VKRenderer3D> renderer);

		// 离屏 target（最终画面 + 各阶段的中间目标）由这里的帧流水线管理。宿主只报告视口尺寸；
		// 内部依次暂停帧循环、停流水线、重建 target、恢复原先在跑的部分，宿主不需要自己安排顺序。
		bool CreateRenderTarget(uint32_t width, uint32_t height);
		bool ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize = {});

		// 生命周期约定：
		//   Start / StopFrameLoop  - Start 会先启动流水线，再启动帧循环线程；StopFrameLoop 只停帧循环，流水线不动。
		//                            都可以重复调用。
		//   Shutdown               - 停帧循环 → 停流水线 → 释放 2D / UI 的 GPU 资源；之后仍可以重新 Start。
		bool Start();
		void StopFrameLoop();
		bool IsFrameLoopRunning() const;
		void Shutdown();

		bool HasRenderer() const;
		bool IsRenderDeviceReady() const;
		bool IsRenderTargetReady() const;
		uint32_t GetRenderTargetWidth() const;
		uint32_t GetRenderTargetHeight() const;

		// 帧循环的节拍。0 表示“流水线能跑多快就跑多快”。新值由帧循环线程在下一轮迭代时生效
		void SetTargetFps(uint32_t fps) noexcept;
		uint32_t GetTargetFps() const noexcept;

		std::shared_ptr<VKFramePipeline> GetFramePipeline() const;
		// 渲染性能统计通道（编辑器「调试 → 渲染性能」）
		std::shared_ptr<RenderProfilerChannel> GetRenderProfiler() const;

		// 宿主只面对这一个入口：等流水线画完的一帧（3D + 2D），在宿主线程上叠 UI，回调里拿到的就是合成结果。
		// 回调里拿到的纹理在下一次 WaitImage 取到新帧之前一直归宿主用，流水线不会往上画别的帧。
		using WaitImageCallback = std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>;
		void WaitImage(const WaitImageCallback& callback);

		// 三层各自的显示开关（编辑器视口用）：3D 关掉时整帧不跑 3D 渲染；2D / UI 关掉时跳过那一层。
		// 3D / 2D 的开关在取帧时记进这一帧，所以要等流水线里已有的一两帧画完才看得到效果
		struct LayerVisibility
		{
			bool layer3D = true;
			bool layer2D = true;
			bool layerUI = true;
		};
		void SetLayerVisibility(const LayerVisibility& visibility);
		LayerVisibility GetLayerVisibility() const;

		void BindRenderSystem(RenderSystem& renderSystem);
		void RegisterRenderSystem(World& world);

	private:
		// 流水线里各阶段的下标（SetVulkanRenderer 按这个顺序注册）
		static constexpr uint32_t kStage3D = 0;
		static constexpr uint32_t kStage2D = 1;
		static constexpr uint32_t kStageCount = 2;

		using SceneFrameBuffer = TripleBuffer<std::shared_ptr<RenderSceneFrameData>>;

		void FrameLoop();
		void SubmitFrame(const std::shared_ptr<VKFramePipeline>& pipeline, const std::shared_ptr<RenderSceneFrameData>& frame);

		// 内部实现要求无锁；调用方已持有 _lifecycleMutex。
		bool StartLocked();
		void StopFrameLoopLocked();
		bool ResizeRenderTargetLocked(const std::shared_ptr<VKFramePipeline>& pipeline,
			uint32_t width, uint32_t height, const std::function<void()>& beforeResize);

	private:
		std::unique_ptr<Renderer3D> _renderer3D;
		std::unique_ptr<Renderer2D> _renderer2D;
		std::unique_ptr<RendererUI> _uiRender;
		std::function<void(const std::vector<UI::UiEvent>&)> _uiEventSink;
		std::atomic<bool> _layer2DVisible{ true };
		std::atomic<bool> _layerUIVisible{ true };

		// 场景帧数据（3D + 2D）：RenderSystem 写，帧循环取
		std::shared_ptr<SceneFrameBuffer> _sceneFrames;
		// 帧循环最近取到的一帧：没有新数据、流水线又快断粮时，拿它再交一次（只在帧循环线程上碰）
		std::shared_ptr<RenderSceneFrameData> _lastSceneFrame;

		std::shared_ptr<VKFramePipeline> _pipeline;
		mutable std::mutex _mutex;
		// 让 target 重建与帧循环的启动/停止互斥；等待帧循环线程收尾时不会持有它。
		std::mutex _lifecycleMutex;
		std::thread _frameThread;
		std::atomic<bool> _stopFrameLoop{ true };
		DynamicFpsController _fpsController;
		std::atomic<uint32_t> _targetFps{ 60 };
		uint32_t _appliedFps = 0xFFFFFFFFu;
	};
}
