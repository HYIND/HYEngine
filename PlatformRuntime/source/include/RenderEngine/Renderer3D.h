#pragma once

#include "RenderEngine/RenderFrameData.h"
#include "VulkanRenderEngine/VKRenderer3D.h"

#include <glm/glm.hpp>

#include <atomic>
#include <memory>
#include <mutex>

namespace Render
{
    // 3D 前端：持有 3D 后端（VKRenderer3D），把场景线程写的 3D 帧数据整理成 RenderState。
    // 帧循环、帧流水线、渲染目标、帧率都在 Render::Renderer 里；这里只做 3D 自己的事。
    class Renderer3D
    {
    public:
        Renderer3D();
        ~Renderer3D();
        Renderer3D(const Renderer3D&) = delete;
        Renderer3D& operator=(const Renderer3D&) = delete;

        // 由 Render::Renderer::SetVulkanRenderer 转过来：它同时把后端挂到新建的帧流水线上
        void SetVulkanRenderer(std::shared_ptr<VKRenderer3D> renderer);
        std::shared_ptr<VKRenderer3D> GetVulkanRenderer() const;
        bool HasRenderer() const { return GetVulkanRenderer() != nullptr; }

        struct CameraSnapshot
        {
            glm::mat4 view{ 1.f };
            glm::mat4 projection{ 1.f };
            glm::vec3 position{ 0.f };
            glm::vec3 direction{ 0.f, 0.f, -1.f };
        };
        bool GetCameraSnapshot(CameraSnapshot& out) const;

        // 帧循环线程上调用：把一帧 3D 数据整理成 RenderState（相机修正、帧数据解析、渲染选项）。
        // width / height 是渲染目标尺寸（算投影的宽高比）；profiling 为 true 时给这一帧挂 profile 并记帧数据解析。
        // 没有后端时返回空
        std::shared_ptr<RenderState> BuildRenderState(const std::shared_ptr<Render3DFrameData>& frame,
            uint32_t width, uint32_t height, bool profiling);

        Scene3DRenderOption GetOption() const;
        void SetOption(Scene3DRenderOption option);

        // 关掉后后端整帧不跑 3D 渲染图，帧数据也不再解析（见 VKRenderer3D::SetSceneRenderEnabled）
        void SetSceneRenderEnabled(bool enabled) noexcept { m_sceneRenderEnabled.store(enabled, std::memory_order_release); }
        bool IsSceneRenderEnabled() const noexcept { return m_sceneRenderEnabled.load(std::memory_order_acquire); }

    private:
        std::shared_ptr<VKRenderer3D> m_vulkanRenderer;
        mutable std::mutex m_mutex;
        Scene3DRenderOption m_option{};
        bool m_optionDirty = false;

        std::atomic<bool> m_sceneRenderEnabled{ true };

        // 最近一帧的相机矩阵
        mutable std::mutex m_cameraMutex;
        glm::mat4 m_lastView{ 1.f };
        glm::mat4 m_lastProjection{ 1.f };
        glm::vec3 m_lastPosition{ 0.f };
        glm::vec3 m_lastDirection{ 0.f, 0.f, -1.f };
        bool m_hasCameraSnapshot = false;
    };
}
