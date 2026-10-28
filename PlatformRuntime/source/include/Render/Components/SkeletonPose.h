#pragma once

#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

#include <memory>

// 当前骨架姿态的渲染快照
struct SkeletonPose :public IComponent
{
	VKRenderContext::BonePalette palette;

	virtual void OnRemove(const Entity& e) {
		palette.matrices.clear();
	}
};

HY_DECLARE_ECS_TYPE(SkeletonPose, "engine.skeleton_pose");
