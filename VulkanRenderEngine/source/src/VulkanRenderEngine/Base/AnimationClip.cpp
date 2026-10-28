#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/AssimpGlmHelpers.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace
{
	// assimp 有些文件不给 TicksPerSecond，缺省按 25 帧算，免得后面除零。
	constexpr float kDefaultTicksPerSecond = 25.f;

	// 关键帧时间戳递增：找到 i 使 keys[i].timeStamp <= tick < keys[i+1].timeStamp。
	template <typename TKey>
	std::size_t FindKeyIndex(const std::vector<TKey>& keys, float tick)
	{
		auto it = std::upper_bound(keys.begin(), keys.end(), tick,
			[](float value, const TKey& key) { return value < key.timeStamp; });

		if (it == keys.begin())
			return 0;

		return static_cast<std::size_t>(it - keys.begin() - 1);
	}

	float KeyBlendFactor(float left, float right, float tick)
	{
		if (right <= left)
			return 0.f;

		return std::clamp((tick - left) / (right - left), 0.f, 1.f);
	}

	glm::vec3 SamplePosition(const std::vector<KeyPosition>& keys, float tick)
	{
		if (keys.size() == 1)
			return keys[0].position;

		const std::size_t index = FindKeyIndex(keys, tick);
		const std::size_t next = std::min(index + 1, keys.size() - 1);
		const float blend = KeyBlendFactor(keys[index].timeStamp, keys[next].timeStamp, tick);

		return glm::mix(keys[index].position, keys[next].position, blend);
	}

	glm::quat SampleRotation(const std::vector<KeyRotation>& keys, float tick)
	{
		if (keys.size() == 1)
			return glm::normalize(keys[0].orientation);

		const std::size_t index = FindKeyIndex(keys, tick);
		const std::size_t next = std::min(index + 1, keys.size() - 1);
		const float blend = KeyBlendFactor(keys[index].timeStamp, keys[next].timeStamp, tick);

		return glm::normalize(glm::slerp(keys[index].orientation, keys[next].orientation, blend));
	}

	glm::vec3 SampleScale(const std::vector<KeyScale>& keys, float tick)
	{
		if (keys.size() == 1)
			return keys[0].scale;

		const std::size_t index = FindKeyIndex(keys, tick);
		const std::size_t next = std::min(index + 1, keys.size() - 1);
		const float blend = KeyBlendFactor(keys[index].timeStamp, keys[next].timeStamp, tick);

		return glm::mix(keys[index].scale, keys[next].scale, blend);
	}
}

AnimationClip::AnimationClip(const std::string& animationPath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "AnimationClip::ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	if (scene->mNumAnimations <= 0)
		return;

	const SkeletonIndex index = BuildSkeletonIndex(scene->mRootNode);
	Reset(scene->mAnimations[0], index);
}

AnimationClip::AnimationClip(const aiScene* scene, int index)
{
	if (!scene || !scene->mRootNode)
		return;

	if (index < 0 || index >= static_cast<int>(scene->mNumAnimations))
		return;

	const SkeletonIndex skeletonIndex = BuildSkeletonIndex(scene->mRootNode);
	Reset(scene->mAnimations[index], skeletonIndex);
}

AnimationClip::~AnimationClip()
{
}

void AnimationClip::Reset(const aiAnimation* aiAni, const SkeletonIndex& index)
{
	m_Name = aiAni->mName.C_Str();
	m_Duration = static_cast<float>(aiAni->mDuration);
	m_TicksPerSecond = static_cast<float>(aiAni->mTicksPerSecond);

	if (m_TicksPerSecond <= 0.f)
		m_TicksPerSecond = kDefaultTicksPerSecond;

	m_SkeletonNodeCount = index.skeleton.nodes.size();
	m_ChannelOfNode.assign(m_SkeletonNodeCount, -1);
	m_Channels.clear();

	ReadChannels(aiAni, index);
}

void AnimationClip::ReadChannels(const aiAnimation* aiAni, const SkeletonIndex& index)
{
	int unresolved = 0;

	m_Channels.reserve(aiAni->mNumChannels);

	for (unsigned int i = 0; i < aiAni->mNumChannels; ++i)
	{
		const aiNodeAnim* aiChannel = aiAni->mChannels[i];

		// aiNodeAnim 里只有名字、没有节点指针，只能按名字找回 id；
		// assimp 的文档保证 channel 指向的节点名唯一。
		const SkeletonNodeIndex nodeIndex = index.skeleton.FindUniqueNodeByName(aiChannel->mNodeName.C_Str());

		if (nodeIndex == SkeletonNode::InvalidIndex || nodeIndex >= m_SkeletonNodeCount)
		{
			++unresolved;
			continue;
		}

		if (m_ChannelOfNode[nodeIndex] >= 0)
			continue;	// 同一节点两条 channel，取第一条

		Channel channel;
		channel.nodeIndex = nodeIndex;

		channel.positions.reserve(aiChannel->mNumPositionKeys);
		for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; ++k)
		{
			KeyPosition key;
			key.position = AssimpGLMHelpers::GetGLMVec(aiChannel->mPositionKeys[k].mValue);
			key.timeStamp = static_cast<float>(aiChannel->mPositionKeys[k].mTime);
			channel.positions.push_back(key);
		}

		channel.rotations.reserve(aiChannel->mNumRotationKeys);
		for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; ++k)
		{
			KeyRotation key;
			key.orientation = AssimpGLMHelpers::GetGLMQuat(aiChannel->mRotationKeys[k].mValue);
			key.timeStamp = static_cast<float>(aiChannel->mRotationKeys[k].mTime);
			channel.rotations.push_back(key);
		}

		channel.scales.reserve(aiChannel->mNumScalingKeys);
		for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; ++k)
		{
			KeyScale key;
			key.scale = AssimpGLMHelpers::GetGLMVec(aiChannel->mScalingKeys[k].mValue);
			key.timeStamp = static_cast<float>(aiChannel->mScalingKeys[k].mTime);
			channel.scales.push_back(key);
		}

		m_ChannelOfNode[nodeIndex] = static_cast<int32_t>(m_Channels.size());
		m_Channels.push_back(std::move(channel));
	}

	if (unresolved > 0)
	{
		std::cout << "AnimationClip[" << m_Name << "]::WARNING:: " << unresolved
			<< " 条曲线按名字找不到骨架节点，已跳过（骨架和动画不是同一个 fbx，或节点重名）"
			<< std::endl;
	}
}

const std::string& AnimationClip::GetName() const
{
	return m_Name;
}

float AnimationClip::GetDuration() const
{
	return m_Duration;
}

float AnimationClip::GetTicksPerSecond() const
{
	return m_TicksPerSecond;
}

float AnimationClip::GetDurationSeconds() const
{
	if (m_TicksPerSecond <= 0.f)
		return 0.f;

	return m_Duration / m_TicksPerSecond;
}

std::size_t AnimationClip::GetSkeletonNodeCount() const
{
	return m_SkeletonNodeCount;
}

const std::vector<AnimationClip::Channel>& AnimationClip::GetChannels() const
{
	return m_Channels;
}

bool AnimationClip::HasChannel(SkeletonNodeIndex nodeIndex) const
{
	if (nodeIndex >= m_ChannelOfNode.size())
		return false;

	return m_ChannelOfNode[nodeIndex] >= 0;
}

glm::mat4 AnimationClip::SampleLocal(SkeletonNodeIndex nodeIndex, float tick, const glm::mat4& bindLocalTransform) const
{
	if (nodeIndex >= m_ChannelOfNode.size())
		return bindLocalTransform;

	const int32_t channelIndex = m_ChannelOfNode[nodeIndex];

	if (channelIndex < 0)
		return bindLocalTransform;

	const Channel& channel = m_Channels[channelIndex];

	// assimp 的 channel 三条曲线是成套出现的，缺一条就当这个节点没有曲线，
	// 否则只能拿单位矩阵顶上，反而会把绑定姿态弄丢。
	if (channel.positions.empty() || channel.rotations.empty() || channel.scales.empty())
		return bindLocalTransform;

	const glm::vec3 translation = SamplePosition(channel.positions, tick);
	const glm::quat rotation = SampleRotation(channel.rotations, tick);
	const glm::vec3 scale = SampleScale(channel.scales, tick);

	return glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
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
	result.reserve(scene->mNumAnimations);

	for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
		result.push_back(std::make_shared<AnimationClip>(scene, static_cast<int>(i)));

	return result;
}
