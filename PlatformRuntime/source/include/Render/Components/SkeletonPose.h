#pragma once

#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

#include <memory>
#include <vector>

#include <glm/glm.hpp>

// 当前骨架姿态的渲染快照。
// 这里存的是骨架每个节点的**全局变换**（不含逆绑定矩阵）：
struct SkeletonPose :public IComponent
{
	std::vector<glm::mat4> globalTransforms;

	virtual void OnRemove(const Entity& e) {
		globalTransforms.clear();
	}
};

HY_DECLARE_ECS_TYPE(SkeletonPose, "engine.skeleton_pose");
