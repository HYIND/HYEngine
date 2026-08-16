#include "OpenGLRenderEngine/Base/Animator.h"

Animator::Animator(std::shared_ptr<Animation> animation, std::shared_ptr<Skeleton> skeleton)
{
	SetAnimation(animation);
	SetSkeleton(skeleton);
}

void Animator::UpdateTime(float dtSecond)
{
	if (!m_Skeleton || !m_CurrentAnimation)
		return;

	for (auto& mat : m_FinalBoneMatrices)
		mat = glm::mat4(1.0f);

	m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dtSecond;
	m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
	CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));

}

void Animator::SetTime(float second)
{
	if (!m_Skeleton || !m_CurrentAnimation)
		return;

	for (auto& mat : m_FinalBoneMatrices)
		mat = glm::mat4(1.0f);

	m_CurrentTime = m_CurrentAnimation->GetTicksPerSecond() * second;
	m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
	CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));
}


void Animator::SetSkeleton(std::shared_ptr<Skeleton> skeleton)
{
	m_Skeleton = skeleton;

	if (m_Skeleton)
	{
		size_t boneSize = m_Skeleton->BoneInfoMap.size();
		m_FinalBoneMatrices.resize(boneSize);
		for (auto& mat : m_FinalBoneMatrices)
			mat = glm::mat4(1.0f);
	}
}

void Animator::SetAnimation(std::shared_ptr<Animation> animation)
{
	m_CurrentTime = 0.0f;
	m_CurrentAnimation = animation;
}

void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform)
{
	std::string nodeName = node->name;
	glm::mat4 nodeTransform = node->transformation;

	const Bone* Bone = m_CurrentAnimation->GetBone(nodeName);

	if (Bone)
		nodeTransform = Bone->GetLocalTransform(m_CurrentTime);

	glm::mat4 globalTransformation = parentTransform * nodeTransform;

	auto& boneInfoMap = m_Skeleton->BoneInfoMap;
	if (boneInfoMap.find(nodeName) != boneInfoMap.end())
	{
		int index = boneInfoMap[nodeName].id;
		glm::mat4 offset = boneInfoMap[nodeName].offset;
		if (index < m_FinalBoneMatrices.size())
			m_FinalBoneMatrices[index] = globalTransformation * offset;
	}

	for (int i = 0; i < node->childrenCount; i++)
		CalculateBoneTransform(&node->children[i], globalTransformation);
}

std::vector<glm::mat4>& Animator::GetFinalBoneMatrices()
{
	return m_FinalBoneMatrices;
}

float Animator::GetDurationTime()
{
	if (!m_CurrentAnimation)
		return 0.f;
	return m_CurrentAnimation->GetDuration() / m_CurrentAnimation->GetTicksPerSecond();
}

