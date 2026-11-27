#pragma once

#include "stdafx.h"
#include "ECSCore/Entity.h"
#include "glm/glm.hpp"

#include <vector>

class EditorApp;

// 编辑器视口图标：给没有网格的 3D 实体（灯光、相机、DDGI 体积、特效发射器、音源、空实体）画一个屏幕空间图标，并支持点选。
// 有网格的实体走实体 ID 缓冲拾取（见 EditorPickPass），不画图标；只有 Transform2D 的 2D 实体走 2D 拾取。
//   - 收集在场景线程上做（异步，界面用上一份结果，不阻塞界面线程）
//   - 图标用 ImDrawList 画矢量图形，不需要贴图资源；固定屏幕尺寸，远处逐渐变淡，选中的始终画出来
//   - 点选按离相机最近的那个；图标总是画在最上面，所以点选也排在 2D / 3D 之前
namespace ViewportIcons
{
	// 视口工具栏的「图标」开关
	bool& Visible();

	// 每帧在 Scene View 里、手柄之前调用（手柄盖在图标上面）。
	// viewProjGL：投影已翻回 GL 约定（projection[1][1] *= -1，同 ImGuizmo / DDGIEditor::DrawViewportOverlay）
	// selected：当前选中的实体（多选时全部），它们的图标画选中圈、远处也不淡出
	void Draw(EditorApp* app, const std::vector<Entity>& selected, const glm::mat4& viewProjGL, const glm::vec3& cameraPos,
		const ImVec2& viewportPos, const ImVec2& viewportSize);

	// 这一帧不画图标（3D 层关掉、没有相机等）：清掉上一帧的屏幕矩形，Pick 什么都点不中
	void Clear();

	// 用本帧 Draw 算出的屏幕矩形点选：返回鼠标下离相机最近的图标的实体，没有返回空实体。必须在同一帧的 Draw 之后调用
	Entity Pick(EditorApp* app, const ImVec2& mouse);

	// 框选：图标中心落在屏幕矩形 [min, max] 里的实体（本帧 Draw 之后有效；太淡、不能点选的图标不算）
	std::vector<Entity> PickRect(EditorApp* app, const ImVec2& min, const ImVec2& max);

	// 鼠标是否在某个图标上（本帧 Draw 之后有效）
	bool IsHovered();
}
