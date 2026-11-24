#pragma once

#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

#include <memory>
#include <vector>

#include <glm/glm.hpp>

// 当前骨架姿态的渲染快照。
// 这里存的是骨架每个节点的**全局变换**（不含逆绑定矩阵）
struct SkeletonPose :public IComponent
{
	SkeletonPose() = default;

	std::vector<glm::mat4> globalTransforms;
	std::shared_ptr<VKRenderContext::SkinnedPoseHistory> history = std::make_shared<VKRenderContext::SkinnedPoseHistory>();

	// AnimationSystem 的静止姿态缓存：姿态是「停在 Default 第 0 帧」或「绑定姿态」时记下来源，
	// 下一帧来源没变（同一个 animator / 同一副骨架）就不重算、不拷贝。
	// 只要有动画在播（含 Idle），来源就清成 None，回到静止时会重算一次。
	enum class StaticSource : uint8_t { None = 0, DefaultFirstFrame, BindPose };
	StaticSource staticSource = StaticSource::None;
	std::weak_ptr<const void> staticKey;	// DefaultFirstFrame：Animator；BindPose：Skeleton

	void ClearStaticCache()
	{
		staticSource = StaticSource::None;
		staticKey.reset();
	}

	virtual void OnRemove(const Entity& e) {
		globalTransforms.clear();
		history = std::make_shared<VKRenderContext::SkinnedPoseHistory>();
		ClearStaticCache();
	}
	// 拷贝不带姿态，缓存也保持默认（None），拷出来的那份第一帧会重算
	SkeletonPose(const SkeletonPose& other) {
		history = std::make_shared<VKRenderContext::SkinnedPoseHistory>();
	}
};

HY_DECLARE_ECS_TYPE(SkeletonPose, "engine.skeleton_pose");
