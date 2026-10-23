#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

AnimationClip::AnimationClip(const std::string& AnimationClipPath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(AnimationClipPath, aiProcess_Triangulate);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "AnimationClip::ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	if (scene->mNumAnimations <= 0)
		return;

	auto AnimationClip = scene->mAnimations[0];
	m_Duration = AnimationClip->mDuration;
	m_TicksPerSecond = AnimationClip->mTicksPerSecond;

	ReadHierarchyData(m_RootNode, scene->mRootNode);
	ReadBones(AnimationClip);
}

AnimationClip::AnimationClip(const aiScene* scene, int index)
{
	if (!scene)
		return;

	auto AnimationClip = scene->mAnimations[index];
	m_Duration = AnimationClip->mDuration;
	m_TicksPerSecond = AnimationClip->mTicksPerSecond;

	ReadHierarchyData(m_RootNode, scene->mRootNode);
	ReadBones(AnimationClip);
}

AnimationClip::~AnimationClip()
{
}

const Bone* AnimationClip::GetBone(const std::string& name)
{
	auto iter = std::find_if(m_Bones.begin(), m_Bones.end(),
		[&](const Bone& Bone)
		{
			return Bone.GetBoneName() == name;
		}
	);
	if (iter == m_Bones.end()) return nullptr;
	else return &(*iter);
}

bool AnimationClip::TryGetCameraTransform(glm::mat4& mat)
{
	return FindCameraTransForm(m_RootNode, mat);
}

float AnimationClip::GetTicksPerSecond() const
{
	return m_TicksPerSecond;
}

float AnimationClip::GetDuration() const
{
	return m_Duration;
}

const AssimpNodeData& AnimationClip::GetRootNode()
{
	return m_RootNode;
}

void AnimationClip::ReadBones(const aiAnimation* aiAni)
{
	int size = aiAni->mNumChannels;

	//reading channels(bones engaged in an AnimationClip and their keyframes)
	for (int i = 0; i < size; i++)
	{
		auto channel = aiAni->mChannels[i];
		std::string boneName = channel->mNodeName.data;

		m_Bones.emplace_back(boneName, channel);
	}
}

void AnimationClip::ReadHierarchyData(AssimpNodeData& dest, const aiNode* src)
{
	assert(src);

	dest.name = src->mName.data;
	dest.transformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(src->mTransformation);
	dest.childrenCount = src->mNumChildren;

	for (int i = 0; i < src->mNumChildren; i++)
	{
		AssimpNodeData newData;
		ReadHierarchyData(newData, src->mChildren[i]);
		dest.children.push_back(newData);
	}
}

bool AnimationClip::FindCameraTransForm(AssimpNodeData& data, glm::mat4& mat)
{
	if (data.name == "Camera" || data.name == "camera")
	{
		mat = data.transformation;
		return true;
	}

	for (auto& child : data.children)
	{
		if (FindCameraTransForm(child, mat))
			return true;
	}
	return false;
}

std::vector<std::shared_ptr<AnimationClip>> AnimationClipLoader::LoadAnimationFile(const std::string& animationPath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "AnimationClipLoader::ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return {};
	}

	if (scene->mNumAnimations <= 0)
		return {};

	std::vector<std::shared_ptr<AnimationClip>> result;
	for (int i = 0; i < scene->mNumAnimations; i++)
	{
		//std::string str = std::format("animationPath=[{}] aniindex=[{}] aniname=[{}]", AnimationClipPath, i, scene->mAnimationClips[i]->mName.C_Str());
		//std::cout << str << '\n';
		auto ani = std::make_shared<AnimationClip>(scene, i);
		result.push_back(ani);
	}
	return result;
}
