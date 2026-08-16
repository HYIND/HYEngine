#include "OpenGLRenderEngine/Base/Animation.h"

Animation::Animation(const std::string& animationPath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "Animation::ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	if (scene->mNumAnimations <= 0)
		return;

	auto animation = scene->mAnimations[0];
	m_Duration = animation->mDuration;
	m_TicksPerSecond = animation->mTicksPerSecond;

	ReadHierarchyData(m_RootNode, scene->mRootNode);
	ReadBones(animation);
}

Animation::Animation(const aiScene* scene, int index)
{
	if (!scene)
		return;

	auto animation = scene->mAnimations[index];
	m_Duration = animation->mDuration;
	m_TicksPerSecond = animation->mTicksPerSecond;

	ReadHierarchyData(m_RootNode, scene->mRootNode);
	ReadBones(animation);
}

Animation::~Animation()
{
}

const Bone* Animation::GetBone(const std::string& name)
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

bool Animation::TryGetCameraTransform(glm::mat4& mat)
{
	return FindCameraTransForm(m_RootNode, mat);
}

float Animation::GetTicksPerSecond() const
{
	return m_TicksPerSecond;
}

float Animation::GetDuration() const
{
	return m_Duration;
}

const AssimpNodeData& Animation::GetRootNode()
{
	return m_RootNode;
}

void Animation::ReadBones(const aiAnimation* animation)
{
	int size = animation->mNumChannels;

	//reading channels(bones engaged in an animation and their keyframes)
	for (int i = 0; i < size; i++)
	{
		auto channel = animation->mChannels[i];
		std::string boneName = channel->mNodeName.data;

		m_Bones.emplace_back(boneName, channel);
	}
}

void Animation::ReadHierarchyData(AssimpNodeData& dest, const aiNode* src)
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

bool Animation::FindCameraTransForm(AssimpNodeData& data, glm::mat4& mat)
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

std::vector<std::shared_ptr<Animation>> AnimationLoader::LoadAnimationFile(const std::string& animationPath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "AnimationLoader::ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return {};
	}

	if (scene->mNumAnimations <= 0)
		return {};

	std::vector<std::shared_ptr<Animation>> result;
	for (int i = 0; i < scene->mNumAnimations; i++)
	{
		//std::string str = std::format("animationPath=[{}] aniindex=[{}] aniname=[{}]", animationPath, i, scene->mAnimations[i]->mName.C_Str());
		//std::cout << str << '\n';
		auto ani = std::make_shared<Animation>(scene, i);
		result.push_back(ani);
	}
	return result;
}
