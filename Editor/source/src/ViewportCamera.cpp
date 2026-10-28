#include "ViewportCamera.h"
#include "EngineRuntime.h"
#include "EditorTags.h"
#include "ECSCore/World.h"
#include "CommonSystems.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "Input/EngineInputContext.h"
#include "Helper/DynamicFpsController.h"

#include <algorithm>
#include <cmath>

void ViewportCamera::RotateCamera(EngineRuntime& runtime, World& world, float deltaX, float deltaY)
{
    if (!world.HasMainCameraEntity())
        return;
    auto entity = world.GetMainCameraEntity();
    if (!entity)
        return;

    if (auto follow = entity.tryGetComponent<CameraFollow>(); follow && follow->target && follow->target.hasComponent<TagEditorCamera>())
    {
        // 旋转不做数学：把鼠标增量交给输入系统，由 CameraFollowSystem 统一消费，
        // 保证和 WASD 移动用的是同一份 yaw/pitch。
        if (auto inputContext = runtime.GetInputContext())
            inputContext->SetMouseDelta(deltaX, deltaY);
    }
}

// 相机被自由相机实体跟随时，位置由 CameraFollowSystem 每帧从相机实体回写。
// 平移改的是相机实体，这里顺手同步一次，避免 Gizmo 与拾取用的参考帧
// 在本帧 postUpdate 之前还是旧值。
static void SyncFollowTarget(Entity cameraEntity, const glm::vec3& position)
{
    if (auto follow = cameraEntity.tryGetComponent<CameraFollow>(); follow && follow->target.hasComponent<Transform>())
        follow->target.getComponent<Transform>().position = position;
}

void ViewportCamera::PanCamera(EngineRuntime& runtime, World& world, float deltaX, float deltaY, float viewportHeight)
{
    (void)runtime;
    if (!world.HasMainCameraEntity())
        return;
    Entity entity = world.GetMainCameraEntity();
    if (!entity || !entity.hasComponent<Transform>())
        return;
    if (deltaX == 0.0f && deltaY == 0.0f)
        return;

    if (auto follow = entity.tryGetComponent<CameraFollow>(); follow && follow->target && follow->target.hasComponent<TagEditorCamera>())
    {

        auto& cameraTrans = entity.getComponent<Transform>();

        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        float fov = 80.0f;
        if (auto cameraComponent = entity.tryGetComponent<CameraComponent>())
        {
            worldUp = cameraComponent->worldUp;
            fov = cameraComponent->fov;
        }

        // 与 MoveLeft/MoveRight/MoveUp/MoveDown 用同一套相机基向量：cameraRight
        // 向右、cameraUp 向上，保证拖拽方向和 WASD 的方向语义一致。
        // CameraFollowSystem 用 quatLookAt(-cameraFront) 生成朝向，因此视线方向是 -(rotation * -Z)。
        const glm::vec3 front = -glm::normalize(cameraTrans.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        const CameraFrontData frontData(front, worldUp);

        // 像素到世界单位：按参考距离处视口实际覆盖的高度换算，远景近景手感一致。
        const float safeHeight = std::max(1.0f, viewportHeight);
        const float worldPerPixel = (2.0f * m_focusDistance * std::tan(glm::radians(fov) * 0.5f)) / safeHeight;

        // 右拖 = 画面向右 → 相机左移；下拖 = 画面向下 → 相机上移。
        cameraTrans.position += (-frontData.cameraRight * deltaX + frontData.cameraUp * deltaY) * worldPerPixel;

        SyncFollowTarget(entity, cameraTrans.position);
    }
}
