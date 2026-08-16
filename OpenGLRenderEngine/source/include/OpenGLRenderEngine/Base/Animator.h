#pragma once

#include <glm/glm.hpp>
#include <map>
#include <vector>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include "OpenGLRenderEngine/Base/Bone.h"
#include "OpenGLRenderEngine/Base/Animation.h"

class Animator
{
public:
	Animator(std::shared_ptr<Animation> animation, std::shared_ptr<Skeleton> skeleton);

	void SetSkeleton(std::shared_ptr<Skeleton> skeleton);		//重设骨骼
	void SetAnimation(std::shared_ptr<Animation> animation);	//切换骨骼动画

	void UpdateTime(float dtSecond);
	void SetTime(float second);

	std::vector<glm::mat4>& GetFinalBoneMatrices();
	float GetDurationTime();
private:
	void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform);

private:
	std::shared_ptr<Animation> m_CurrentAnimation;
	std::shared_ptr<Skeleton> m_Skeleton;

	std::vector<glm::mat4> m_FinalBoneMatrices;
	float m_CurrentTime;
};