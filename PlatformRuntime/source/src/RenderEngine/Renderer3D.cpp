#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/RenderFrameDataAnalysis.h"
#include "CommonData/Camera.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"

#include <atomic>
#include <iostream>

namespace Render
{
    Renderer3D::Renderer3D() = default;

    Renderer3D::~Renderer3D() = default;

    void Renderer3D::SetVulkanRenderer(std::shared_ptr<VKRenderer3D> renderer)
    {
        std::lock_guard lock(m_mutex);
        m_vulkanRenderer = std::move(renderer);
        if (m_vulkanRenderer)
            m_option = m_vulkanRenderer->GetOption();
    }

    std::shared_ptr<VKRenderer3D> Renderer3D::GetVulkanRenderer() const
    {
        std::lock_guard lock(m_mutex);
        return m_vulkanRenderer;
    }

    std::shared_ptr<RenderState> Renderer3D::BuildRenderState(const std::shared_ptr<Render3DFrameData>& frame,
        uint32_t targetWidth, uint32_t targetHeight, bool profiling)
    {
        if (!frame)
            return nullptr;
        auto renderer = GetVulkanRenderer();
        if (!renderer)
            return nullptr;

        const float width = static_cast<float>(targetWidth);
        const float height = static_cast<float>(targetHeight);
        float aspect = (height > 0.f) ? (width / height) : 1.f;

        // NaN 防护：相机参数先整理干净再用。RenderState 里的 direction/up/right 会直接进 shader，
        // 所以修正后的值要写回 frame，不能只在算矩阵时修。
        bool cameraFixed = CameraHelper::SanitizeProjection(aspect, frame->fov, frame->nearPlane, frame->farPlane);
        if (!CameraHelper::IsFinite(frame->position))
        {
            frame->position = glm::vec3(0.f);
            cameraFixed = true;
        }
        cameraFixed |= CameraHelper::SanitizeBasis(frame->direction, frame->directionUp, frame->directionRight);
        if (cameraFixed)
        {
            static std::atomic<bool> warned{ false };
            if (!warned.exchange(true))
                std::cerr << "[ Renderer3D ] WARNING: 3D 帧的相机参数非法（零向量/NaN/近远平面错误），已替换为安全默认值；检查场景主相机。\n";
        }

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
        // 渲染性能统计：窗口开着时才挂 profile，记帧数据解析与入队时刻
        std::shared_ptr<RenderFrameProfile> profile;
        if (profiling)
            profile = std::make_shared<RenderFrameProfile>();

        const bool sceneEnabled = m_sceneRenderEnabled.load(std::memory_order_acquire);
        renderer->SetSceneRenderEnabled(sceneEnabled);

        auto mutableFrame = frame;
        if (sceneEnabled)
        {
            RenderProfileScope scope(profile.get(), RenderProfileStep::Analysis);
            RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(mutableFrame, *state);
        }

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
        if (profile)
        {
            profile->pushTimeUs = RenderProfileNowUs();
            state->profile = std::move(profile);
        }
        return state;
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
