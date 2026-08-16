#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <functional>
#include "OpenGLRenderEngine/Base/Bone.h"
#include "OpenGLRenderEngine/Base/Animation.h"
#include "OpenGLRenderEngine/Base/Model.h"

struct AssimpNodeData
{
	glm::mat4 transformation;
	std::string name;
	int childrenCount;
	std::vector<AssimpNodeData> children;
};

class Animation
{
public:
	Animation(const std::string& animationPath);
	Animation(const aiScene* scene, int index = 0);

	~Animation();

	float GetTicksPerSecond() const;
	float GetDuration() const;

	const AssimpNodeData& GetRootNode();
	const Bone* GetBone(const std::string& name);
	bool TryGetCameraTransform(glm::mat4& mat);		//针对某些场景树自带Camera的情况，如第一人称动画

private:
	void ReadBones(const aiAnimation* animation);
	void ReadHierarchyData(AssimpNodeData& dest, const aiNode* src);

	bool FindCameraTransForm(AssimpNodeData& data, glm::mat4& mat);

private:

	float m_Duration;
	int m_TicksPerSecond;
	std::vector<Bone> m_Bones;
	AssimpNodeData m_RootNode;
};

class AnimationLoader
{
public:
	static std::vector<std::shared_ptr<Animation>> LoadAnimationFile(const std::string& animationPath);
};
