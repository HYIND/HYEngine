#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <assimp/scene.h>
#include <functional>
#include "VulkanRenderEngine/Base/Bone.h"

struct AssimpNodeData
{
	glm::mat4 transformation;
	std::string name;
	int childrenCount;
	std::vector<AssimpNodeData> children;
};

class AnimationClip
{
public:
	AnimationClip(const std::string& animationPath);
	AnimationClip(const aiScene* scene, int index = 0);

	~AnimationClip();

	float GetTicksPerSecond() const;
	float GetDuration() const;

	const AssimpNodeData& GetRootNode();
	const Bone* GetBone(const std::string& name);
	bool TryGetCameraTransform(glm::mat4& mat);		//针对某些场景树自带Camera的情况，如第一人称动画

private:
	void ReadBones(const aiAnimation* aiAni);
	void ReadHierarchyData(AssimpNodeData& dest, const aiNode* src);

	bool FindCameraTransForm(AssimpNodeData& data, glm::mat4& mat);

private:

	float m_Duration;
	int m_TicksPerSecond;
	std::vector<Bone> m_Bones;
	AssimpNodeData m_RootNode;
};

class AnimationClipLoader
{
public:
	static std::vector<std::shared_ptr<AnimationClip>> LoadAnimationFile(const std::string& animationPath);
};
