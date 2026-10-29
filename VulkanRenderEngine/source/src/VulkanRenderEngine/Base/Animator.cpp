#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Animator.h"

#include <cmath>

Animator::Animator(std::shared_ptr<AnimationClip> animation, std::shared_ptr<Skeleton> skeleton)
{
	SetAnimation(animation);
	SetSkeleton(skeleton);
}

void Animator::UpdateTime(float dtSecond)
{
	if (!m_Skeleton || !m_CurrentAnimation)
		return;

	m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dtSecond;
	m_CurrentTime = std::fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());

	Rebuild();
}

void Animator::SetTime(float second)
{
	if (!m_Skeleton)
		return;

	if (!m_CurrentAnimation)
	{
		Rebuild();
		return;
	}

	m_CurrentTime = m_CurrentAnimation->GetTicksPerSecond() * second;
	m_CurrentTime = std::fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());

	Rebuild();
}

void Animator::SetSkeleton(std::shared_ptr<Skeleton> skeleton)
{
	m_Skeleton = skeleton;

	if (!m_Skeleton)
	{
		m_FinalBoneMatrices.clear();
		m_GlobalTransforms.clear();
		return;
	}

	m_GlobalTransforms.resize(m_Skeleton->nodes.size());
	m_FinalBoneMatrices.resize(m_Skeleton->nodes.size());

	Rebuild();
}

void Animator::SetAnimation(std::shared_ptr<AnimationClip> animation)
{
	m_CurrentTime = 0.f;
	m_CurrentAnimation = animation;

	if (m_Skeleton)
		Rebuild();
}

void Animator::Rebuild()
{
	if (!m_Skeleton || m_Skeleton->nodes.empty())
	{
		m_FinalBoneMatrices.clear();
		m_GlobalTransforms.clear();
		return;
	}

	const std::vector<SkeletonNode>& nodes = m_Skeleton->nodes;

	m_GlobalTransforms.resize(nodes.size());
	m_FinalBoneMatrices.resize(nodes.size());

	// 节点是先父后子编号的，父下标一定小于子下标，所以一趟顺序遍历就够，不需要递归。
	for (std::size_t i = 0; i < nodes.size(); ++i)
	{
		const SkeletonNode& node = nodes[i];

		const glm::mat4 local = m_CurrentAnimation
			? m_CurrentAnimation->SampleLocal(static_cast<SkeletonNodeIndex>(i), m_CurrentTime, node.bindLocalTransform)
			: node.bindLocalTransform;

		const glm::mat4 global = (node.parentIndex == SkeletonNode::InvalidIndex)
			? local
			: m_GlobalTransforms[node.parentIndex] * local;

		m_GlobalTransforms[i] = global;
		m_FinalBoneMatrices[i] = global * node.inverseBindTransform;
	}
}

std::vector<glm::mat4>& Animator::GetFinalBoneMatrices()
{
	return m_FinalBoneMatrices;
}

float Animator::GetCurrentTime() const
{
	if (!m_CurrentAnimation)
		return 0.f;

	const float ticksPerSecond = m_CurrentAnimation->GetTicksPerSecond();
	if (ticksPerSecond <= 0.f)
		return 0.f;

	return m_CurrentTime / ticksPerSecond;
}

float Animator::GetDurationTime()
{
	if (!m_CurrentAnimation)
		return 0.f;

	return m_CurrentAnimation->GetDurationSeconds();
}
