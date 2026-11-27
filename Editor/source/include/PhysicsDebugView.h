#pragma once

#include "stdafx.h"
#include "ECSCore/Entity.h"
#include "glm/glm.hpp"

#include <vector>

class EditorApp;

// 编辑器视口的碰撞体显示：画 Bullet 世界里实际参与碰撞的形状（PhysicsSystem::CollectDebugLines）。
//   - 线段在场景线程上收集（异步，界面用上一份结果，不阻塞界面线程），用 ImDrawList 投影画出来，没有深度测试
//   - 「全部」模式只画相机附近、视锥内的物体；三角网格只画相机周围的三角形；线段总数有上限
//   - 颜色按类型：静态灰绿、动态绿（休眠时暗）、运动学黄、传感器紫、角色青；选中的更亮、更粗
namespace PhysicsDebugView
{
	enum class Mode : int { Off = 0, Selected, All };

	// 视口工具栏上的控件（碰撞体：关 / 选中 / 全部，打开时还有 AABB、接触点两个开关）
	void DrawToolbar();

	Mode GetMode();
	void SetMode(Mode mode);

	// 属性面板悬停在某个碰撞形状上时，每帧调用一次（childIndex：Physics::collisionShape.children 的下标）。
	// 本帧视口里把这个形状画成高亮；显示模式是「关」时也临时画出这个实体的碰撞体。只对下一次 Draw 有效
	void SetHoveredShape(Entity entity, int childIndex);

	// 每帧在 Scene View 里、手柄之前调用（手柄盖在上面）。3D 层关掉、没有相机时不要调用
	// viewProjGL：投影已翻回 GL 约定（projection[1][1] *= -1，同视口图标 / 包围盒）
	void Draw(EditorApp* app, const std::vector<Entity>& selection, const glm::mat4& viewProjGL, const glm::vec3& cameraPos,
		const ImVec2& viewportPos, const ImVec2& viewportSize);
}
