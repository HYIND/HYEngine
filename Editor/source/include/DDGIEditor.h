#pragma once

#include "stdafx.h"
#include "ECSCore/Entity.h"
#include "ECSCore/IComponent.h"
#include "glm/glm.hpp"

class EditorApp;
class ProjectManager;

// DDGI 编辑器工具：体积组件面板、单探针编辑、探针 / 体积可视化叠加、漏光调试窗口。
// 与渲染器之间只通过 DDGIDebugChannel 交换数据（见 VulkanRenderEngine/General/DDGIDebug.h）。
namespace DDGIEditor
{
	// 每帧在场景视口绘制前调用：推送调试设置、处理烘焙结果
	void UpdateFrame(EditorApp* app, Entity selected);

	// 视口鼠标位置（视口内归一化坐标），漏光调试跟随它
	void SetViewportHover(bool hovered, const glm::vec2& uv);

	// 探针编辑模式下的视口点击：命中探针返回 true（调用方不再做实体拾取）
	bool HandleViewportClick(Entity selected, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, bool additive);

	// 视口叠加：体积线框、选中的探针、漏光调试连线。projectionGL 为翻转回 GL 约定的投影（同 ImGuizmo）
	void DrawViewportOverlay(EditorApp* app, Entity selected, const glm::mat4& view, const glm::mat4& projectionGL,
		const glm::vec2& viewportPos, const glm::vec2& viewportSize);

	// 「Options」窗口里的调试选项与运行时统计
	void DrawDebugOptions(EditorApp* app);

	// 漏光调试结果窗口（打开漏光调试时显示）
	void DrawLeakWindow(EditorApp* app);

	// 组件面板（注册到 PropertiesHelper 的自定义绘制）
	void DrawVolumeComponent(Entity entity, IComponent* component, ProjectManager* projectManager);

	// Gizmo 松开后：缩放折算成探针数、去掉旋转、中心吸附到网格
	void FinalizeVolumeTransform(Entity entity);

	bool IsVolume(Entity entity);
}
