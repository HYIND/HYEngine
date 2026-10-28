#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/Skeleton.h"


class Animator
{
public:
	Animator(std::shared_ptr<AnimationClip> animation, std::shared_ptr<Skeleton> skeleton);

	void SetSkeleton(std::shared_ptr<Skeleton> skeleton);		//重设骨骼
	void SetAnimation(std::shared_ptr<AnimationClip> animation);	//切换骨骼动画

	void UpdateTime(float dtSecond);
	void SetTime(float second);

	std::vector<glm::mat4>& GetFinalBoneMatrices();

	float GetDurationTime();

private:
	void Rebuild();

private:
	std::shared_ptr<AnimationClip> m_CurrentAnimation;
	std::shared_ptr<Skeleton> m_Skeleton;

	std::vector<glm::mat4> m_FinalBoneMatrices;
	std::vector<glm::mat4> m_GlobalTransforms;	// 复用同一块，避免每帧分配

	float m_CurrentTime = 0.f;	// 单位是 tick
};
