#pragma once

#include "ECSCore/IComponent.h"
#include "VulkanRenderEngine/General/VKRenderContext2D.h"

// 2D 相机。位置（视野中心）和旋转取同一实体的 Transform2D；没有 Transform2D 时中心在设计区域中央。
// 场景里有多个时取 primary 且 priority 最大的那个；一个都没有时用默认视图（[0,1000] x [0,600] 拉伸）。
struct Camera2D : public IComponent
{
	glm::vec2 designSize = glm::vec2(1000.0f, 600.0f);	// 设计分辨率（世界单位）
	float zoom = 1.0f;
	VKRenderContext::AspectMode2D aspectMode = VKRenderContext::AspectMode2D::Fit;
	bool pixelSnap = false;
	bool primary = true;
	int priority = 0;
};

HY_DECLARE_ECS_TYPE(Camera2D, "hy.engine.component.Camera2D");
