#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "VulkanRenderEngine/Base/Skeleton.h"

struct KeyPosition
{
	glm::vec3 position;
	float timeStamp;
};

struct KeyRotation
{
	glm::quat orientation;
	float timeStamp;
};

struct KeyScale
{
	glm::vec3 scale;
	float timeStamp;
};

class AnimationClip
{
public:
	struct Channel
	{
		SkeletonNodeIndex nodeIndex = 0;
		std::vector<KeyPosition> positions;
		std::vector<KeyRotation> rotations;
		std::vector<KeyScale> scales;
	};

	AnimationClip(const std::string& animationPath);
	AnimationClip(const aiScene* scene, int index = 0);

	~AnimationClip();

	const std::string& GetName() const;

	// 时长单位是 tick，换算成秒要除以 TicksPerSecond。
	float GetDuration() const;
	float GetTicksPerSecond() const;
	float GetDurationSeconds() const;

	// 曲线可索引的范围，等于骨架节点数。
	std::size_t GetSkeletonNodeCount() const;

	const std::vector<Channel>& GetChannels() const;
	bool HasChannel(SkeletonNodeIndex nodeIndex) const;

	glm::mat4 SampleLocal(SkeletonNodeIndex nodeIndex, float tick, const glm::mat4& bindLocalTransform) const;

private:
	void Reset(const aiAnimation* aiAni, const SkeletonIndex& index);
	void ReadChannels(const aiAnimation* aiAni, const SkeletonIndex& index);

private:
	std::string m_Name;
	float m_Duration = 0.f;
	float m_TicksPerSecond = 25.f;
	std::size_t m_SkeletonNodeCount = 0;
	std::vector<Channel> m_Channels;
	std::vector<int32_t> m_ChannelOfNode;	// 和骨架节点一一对应，-1 表示这个节点没有曲线
};

class AnimationClipLoader
{
public:
	static std::vector<std::shared_ptr<AnimationClip>> LoadAnimationFile(const std::string& animationPath);
};
