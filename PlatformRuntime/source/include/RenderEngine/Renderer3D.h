#pragma once

#include "Helper/DynamicFpsController.h"
#include "Helper/TripleBuffer.h"
#include "RenderEngine/RenderFrameManager.h"
#include "VulkanRenderEngine/VulkanRenderer.h"

#include <glm/glm.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace Render
{
    class Renderer3D
    {
    public:
        using FrameBuffer = TripleBuffer<std::shared_ptr<Render3DFrameData>>;
        using FrameBufferPtr = std::shared_ptr<FrameBuffer>;

        Renderer3D();
        explicit Renderer3D(FrameBufferPtr frames);
        ~Renderer3D();
        Renderer3D(const Renderer3D&) = delete;
        Renderer3D& operator=(const Renderer3D&) = delete;

        FrameBufferPtr GetFrameBuffer() const noexcept { return m_frames; }
        void SetVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer);
        std::shared_ptr<VulkanRenderer> GetVulkanRenderer() const;

        // 生命周期约定：
        //   Start / StopFrameLoop       - 只涉及帧消费线程；开销很小、可重复调用，后端不动。
        //   StartBackend                - Vulkan 分发线程和它的帧池。
        //   Shutdown                    - 先停消费者再停后端；之后服务仍可以重新启动。
        //   Create/ResizeRenderTarget   - 内部依次暂停消费者、停后端、重建 target、恢复原先
        //                                 在跑的部分，宿主不需要自己安排这些调用的顺序。
        bool Start();
        void StopFrameLoop();
        void Shutdown();
        void StartBackend();

        // 离屏 target 的所有权在这里。宿主只交来一个仅创建设备的后端、并报告视口尺寸；
        // target 何时创建或重建由服务决定，后端更换 target 期间帧消费者保持挂起。
        bool CreateRenderTarget(uint32_t width, uint32_t height);
        bool ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize = {});
        bool HasRenderer() const;
        bool IsRenderTargetReady() const;
        bool IsRenderDeviceReady() const;
        uint32_t GetRenderTargetWidth() const;
        uint32_t GetRenderTargetHeight() const;
        bool IsFrameLoopRunning() const noexcept;

        // 帧消费者的节拍。0 表示“后端能跑多快就跑多快”。新值由消费线程在下一轮迭代时
        void SetTargetFps(uint32_t fps) noexcept;
        uint32_t GetTargetFps() const noexcept;

        struct CameraSnapshot
        {
            glm::mat4 view{ 1.f };
            glm::mat4 projection{ 1.f };
            glm::vec3 position{ 0.f };
            glm::vec3 direction{ 0.f, 0.f, -1.f };
        };
        bool GetCameraSnapshot(CameraSnapshot& out) const;

        void ProcessFrame(const std::shared_ptr<Render3DFrameData>& frame);
        void WaitImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback);

        Scene3DRenderOption GetOption() const;
        void SetOption(Scene3DRenderOption option);

    private:
        void FrameLoop();

        // 内部实现要求无锁；调用方已持有 m_lifecycleMutex。
        bool StartLocked();
        void StopFrameLoopLocked();
        bool ResizeRenderTargetLocked(const std::shared_ptr<VulkanRenderer>& renderer,
            uint32_t width, uint32_t height, const std::function<void()>& beforeResize);

        FrameBufferPtr m_frames;
        std::shared_ptr<VulkanRenderer> m_vulkanRenderer;
        mutable std::mutex m_mutex;
        // 让 target 重建与帧循环的启动/停止互斥；等待消费线程收尾时不会持有它。
        std::mutex m_lifecycleMutex;
        std::thread m_frameThread;
        std::atomic<bool> m_stopFrameLoop{ true };
        Scene3DRenderOption m_option{};
        bool m_optionDirty = false;
        DynamicFpsController m_fpsController;

        std::atomic<uint32_t> m_targetFps{ 60 };
        uint32_t m_appliedFps = 0xFFFFFFFFu;

        // 最近一帧的相机矩阵
        mutable std::mutex m_cameraMutex;
        glm::mat4 m_lastView{ 1.f };
        glm::mat4 m_lastProjection{ 1.f };
        glm::vec3 m_lastPosition{ 0.f };
        glm::vec3 m_lastDirection{ 0.f, 0.f, -1.f };
        bool m_hasCameraSnapshot = false;
    };
}
