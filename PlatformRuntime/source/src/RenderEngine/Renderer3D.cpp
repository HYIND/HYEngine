#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/RenderFrameDataAnalysis.h"
#include "CommonData/Camera.h"

#include <algorithm>
#include <thread>

namespace Render
{
    Renderer3D::Renderer3D()
        : Renderer3D(std::make_shared<FrameBuffer>())
    {}

    Renderer3D::Renderer3D(FrameBufferPtr frames)
        : m_frames(std::move(frames))
    {
        if (!m_frames)
            m_frames = std::make_shared<FrameBuffer>();
        for (int index = 0; index < 3; ++index)
            m_frames->setInitialValue(index, std::make_shared<Render3DFrameData>());
    }

    Renderer3D::~Renderer3D()
    {
        Shutdown();
    }

    void Renderer3D::SetVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer)
    {
        std::lock_guard lock(m_mutex);
        m_vulkanRenderer = std::move(renderer);
        if (m_vulkanRenderer)
            m_option = m_vulkanRenderer->GetOption();
    }

    std::shared_ptr<VulkanRenderer> Renderer3D::GetVulkanRenderer() const
    {
        std::lock_guard lock(m_mutex);
        return m_vulkanRenderer;
    }

    bool Renderer3D::Start()
    {
        std::lock_guard lock(m_lifecycleMutex);
        return StartLocked();
    }

    bool Renderer3D::StartLocked()
    {
        if (!m_frames || !GetVulkanRenderer())
            return false;

        StartBackend();
        if (!m_stopFrameLoop.exchange(false))
            return true;
        m_frameThread = std::thread(&Renderer3D::FrameLoop, this);
        return true;
    }

    void Renderer3D::StopFrameLoop()
    {
        std::lock_guard lock(m_lifecycleMutex);
        StopFrameLoopLocked();
    }

    void Renderer3D::StopFrameLoopLocked()
    {
        m_stopFrameLoop = true;
        if (!m_frameThread.joinable())
            return;
        // 从消费线程内部调用（例如在帧循环里提交的任务）：循环会自行退出，
        // 这里再 join 就会死锁。
        if (m_frameThread.get_id() == std::this_thread::get_id())
            return;
        m_frameThread.join();
    }

    void Renderer3D::Shutdown()
    {
        std::lock_guard lock(m_lifecycleMutex);
        StopFrameLoopLocked();
        if (auto renderer = GetVulkanRenderer())
            renderer->Stop();
    }

    void Renderer3D::StartBackend()
    {
        if (auto renderer = GetVulkanRenderer())
            renderer->Run();
    }

    bool Renderer3D::CreateRenderTarget(uint32_t width, uint32_t height)
    {
        std::lock_guard lock(m_lifecycleMutex);

        auto renderer = GetVulkanRenderer();
        if (!renderer || !renderer->GetVulkanDevice())
            return false;

        width = std::max(1u, width);
        height = std::max(1u, height);

        if (renderer->GetWidth() == width && renderer->GetHeight() == height)
            return true;

        // 首个 target：宿主只建了设备，此时还没有交换链和 target。
        if (renderer->GetWidth() == 0 || renderer->GetHeight() == 0)
            return VulkanRenderer::CreateForOffScreen_Target(renderer, width, height);

        return ResizeRenderTargetLocked(renderer, width, height, {});
    }

    bool Renderer3D::ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
    {
        std::lock_guard lock(m_lifecycleMutex);

        auto renderer = GetVulkanRenderer();
        if (!renderer || !renderer->GetVulkanDevice())
            return false;

        width = std::max(1u, width);
        height = std::max(1u, height);

        if (renderer->GetWidth() == width && renderer->GetHeight() == height)
            return false;

        if (renderer->GetWidth() == 0 || renderer->GetHeight() == 0)
        {
            if (!VulkanRenderer::CreateForOffScreen_Target(renderer, width, height))
                return false;
            if (beforeResize)
                beforeResize();
            return true;
        }

        return ResizeRenderTargetLocked(renderer, width, height, beforeResize);
    }

    bool Renderer3D::ResizeRenderTargetLocked(const std::shared_ptr<VulkanRenderer>& renderer,
        uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
    {
        // 后端拆除 target 之前，帧消费者和分发线程都必须已经停下：否则仍可能有一帧被
        // 提交到已销毁的 target，而宿主侧的 vkDeviceWaitIdle 并不构成屏障。
        const bool wasRunning = !m_stopFrameLoop.load();
        StopFrameLoopLocked();
        renderer->Stop();

        if (beforeResize)
            beforeResize();

        // 后端已经停了，这里的 Resize 只负责重建 target。
        renderer->Resize(width, height);

        if (wasRunning)
            StartLocked();
        return true;
    }

    bool Renderer3D::HasRenderer() const
    {
        return GetVulkanRenderer() != nullptr;
    }

    bool Renderer3D::IsRenderTargetReady() const
    {
        auto renderer = GetVulkanRenderer();
        return renderer && renderer->GetWidth() > 0 && renderer->GetHeight() > 0;
    }

    bool Renderer3D::IsRenderDeviceReady() const
    {
        auto renderer = GetVulkanRenderer();
        return renderer && renderer->GetVulkanDevice() != nullptr;
    }

    uint32_t Renderer3D::GetRenderTargetWidth() const
    {
        auto renderer = GetVulkanRenderer();
        return renderer ? renderer->GetWidth() : 0;
    }

    uint32_t Renderer3D::GetRenderTargetHeight() const
    {
        auto renderer = GetVulkanRenderer();
        return renderer ? renderer->GetHeight() : 0;
    }

    bool Renderer3D::IsFrameLoopRunning() const noexcept
    {
        return !m_stopFrameLoop.load();
    }

    void Renderer3D::SetTargetFps(uint32_t fps) noexcept
    {
        m_targetFps.store(fps, std::memory_order_relaxed);
    }

    uint32_t Renderer3D::GetTargetFps() const noexcept
    {
        return m_targetFps.load(std::memory_order_relaxed);
    }

    void Renderer3D::ProcessFrame(const std::shared_ptr<Render3DFrameData>& frame)
    {
        if (!frame)
            return;
        auto renderer = GetVulkanRenderer();
        if (!renderer)
            return;

        const float width = static_cast<float>(renderer->GetWidth());
        const float height = static_cast<float>(renderer->GetHeight());
        const float aspect = (height > 0.f) ? (width / height) : 1.f;
        frame->projection = CameraHelper::GetPerspectiveProjectionMatrix(aspect, frame->fov, frame->nearPlane, frame->farPlane);
        frame->view = CameraHelper::GetViewMatrix(frame->position, frame->direction, frame->directionUp);

        // 给 UI（Gizmo/拾取）留一份快照，免得它也去读这份三缓冲
        {
            std::lock_guard lock(m_cameraMutex);
            m_lastView = frame->view;
            m_lastProjection = frame->projection;
            m_lastPosition = frame->position;
            m_lastDirection = frame->direction;
            m_hasCameraSnapshot = true;
        }

        auto state = RenderStateBuilder()
            .SetCamera(frame->projection, frame->view,
                frame->position, frame->direction, frame->directionUp, frame->directionRight,
                frame->nearPlane, frame->farPlane, frame->fov)
            .Build();
        auto mutableFrame = frame;
        RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(mutableFrame, *state);

        Scene3DRenderOption option;
        bool optionDirty = false;
        {
            std::lock_guard lock(m_mutex);
            option = m_option;
            optionDirty = m_optionDirty;
            m_optionDirty = false;
        }
        if (optionDirty)
            renderer->SetOption(option);
        renderer->PushFrameState(state);
    }

    bool Renderer3D::GetCameraSnapshot(CameraSnapshot& out) const
    {
        std::lock_guard lock(m_cameraMutex);
        if (!m_hasCameraSnapshot)
            return false;

        out.view = m_lastView;
        out.projection = m_lastProjection;
        out.position = m_lastPosition;
        out.direction = m_lastDirection;
        return true;
    }

    void Renderer3D::FrameLoop()
    {
        m_fpsController.reset();
        m_appliedFps = 0xFFFFFFFFu;
        while (!m_stopFrameLoop)
        {
            // 节拍的读取与应用都在消费线程上：控制器只在这里被碰，
            // 宿主侧调用 SetTargetFps() 不会和 run() 竞争。
            const uint32_t targetFps = m_targetFps.load(std::memory_order_relaxed);
            if (targetFps != m_appliedFps)
            {
                if (targetFps != 0)
                    m_fpsController.setCurTargetFps(targetFps);
                m_appliedFps = targetFps;
            }

            ProcessFrame(m_frames->acquireReadBuffer());

            // 0 means uncapped: skip the pacing sleep entirely.
            if (targetFps != 0)
                m_fpsController.run();
        }
    }

    void Renderer3D::WaitImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback)
    {
        if (auto renderer = GetVulkanRenderer())
            renderer->WaitImage(callback);
    }

    Scene3DRenderOption Renderer3D::GetOption() const
    {
        std::lock_guard lock(m_mutex);
        return m_option;
    }

    void Renderer3D::SetOption(Scene3DRenderOption option)
    {
        std::lock_guard lock(m_mutex);
        m_option = option;
        m_optionDirty = true;
    }
}
