#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/Skeleton.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

class Animator
{
public:
	Animator(std::shared_ptr<AnimationClip> animation, std::shared_ptr<Skeleton> skeleton);

	void SetSkeleton(std::shared_ptr<Skeleton> skeleton);		//重设骨骼
	void SetAnimation(std::shared_ptr<AnimationClip> animation);	//切换骨骼动画

	void UpdateTime(float dtSecond);
	void SetTime(float second);

	// 骨架每个节点的全局变换（含缩放/平移，不含逆绑定矩阵）。
	// 逆绑定矩阵是 per-mesh 的（aiBone::mOffsetMatrix 定义在该网格自己的绑定空间里），
	// 由 SkinnedMesh::GetInverseBindMatrices() 提供，最终 palette = global * inverseBind 在渲染侧合成。
	std::vector<glm::mat4>& GetGlobalTransforms();

	float GetDurationTime();
	float GetCurrentTime() const;

private:
	void Rebuild();

private:
	std::shared_ptr<AnimationClip> m_CurrentAnimation;
	std::shared_ptr<Skeleton> m_Skeleton;

	std::vector<glm::mat4> m_GlobalTransforms;	// 复用同一块，避免每帧分配

	float m_CurrentTime = 0.f;	// 单位是 tick
};
