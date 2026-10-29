#pragma once

#include "VulkanRenderEngine/Base/Animator.h"
#include "VulkanRenderEngine/Base/Skeleton.h"
#include "CommonData/AssetGUIDDef.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

#include <unordered_map>
#include <memory>
#include <string>

struct SkeletonAnimatorGroup :public IComponent
{
	struct SkeletonAnimatorData
	{
		std::shared_ptr<Animator> animator;
		std::shared_ptr<AnimationClip> clip;

		AssetGUID clipGuid;

		bool enable = false;
		int maxLoopCount = 0;
		int64_t curPlayTimeMs = 0;

		SkeletonAnimatorData() = default;
		SkeletonAnimatorData(const AssetGUID& clipGuid)
			:clipGuid(clipGuid) {
		}
	};

	AssetGUID skeletonGuid;
	std::shared_ptr<Skeleton> skeleton;
	std::string defaultAnimator;
	std::string idleAnimator;
	std::unordered_map<std::string, std::shared_ptr<SkeletonAnimatorData>> animatorDatas;


	SkeletonAnimatorGroup() = default;
	SkeletonAnimatorGroup(const SkeletonAnimatorGroup& other){
		CloneFrom(other);
	}

	SkeletonAnimatorGroup& operator=(const SkeletonAnimatorGroup& other)
	{
		if (this != &other)
		{
			animatorDatas.clear();
			CloneFrom(other);
		}
		return *this;
	}

	SkeletonAnimatorGroup(SkeletonAnimatorGroup&& other) noexcept = default;
	SkeletonAnimatorGroup& operator=(SkeletonAnimatorGroup&& other) noexcept = default;

	virtual void OnRemove(const Entity& e) {
		skeletonGuid.clear();
		skeleton.reset();
		defaultAnimator.clear();
		idleAnimator.clear();
		animatorDatas.clear();
	}

	bool AddAnimatorSlot(const std::string& name, const AssetGUID& clipGuid)
	{
		if (name.empty() || animatorDatas.find(name) != animatorDatas.end())
			return false;
		animatorDatas[name] = std::make_shared<SkeletonAnimatorData>(clipGuid);
		return true;
	}

	bool AddAnimator(const std::string& name, std::shared_ptr<AnimationClip> ani, std::shared_ptr<Skeleton> skeleton, bool enable = false)
	{
		if (!ani || !skeleton)
			return false;
		if (name.empty() || animatorDatas.find(name) != animatorDatas.end())
			return false;

		auto data = std::make_shared<SkeletonAnimatorData>();
		data->clip = std::move(ani);
		data->animator = std::make_shared<Animator>(data->clip, std::move(skeleton));
		data->enable = enable;
		animatorDatas[name] = std::move(data);
		return true;
	}

	// 换骨架：所有 animator 都要按新骨架重建，palette 的长度是骨架节点数，旧的不可能对上。
	void SetSkeleton(std::shared_ptr<Skeleton> newSkeleton, const AssetGUID& guid)
	{
		skeleton = std::move(newSkeleton);
		skeletonGuid = guid;

		for (auto& [name, data] : animatorDatas)
		{
			if (!data)
				continue;

			if (skeleton && data->clip)
				data->animator = std::make_shared<Animator>(data->clip, skeleton);
			else
				data->animator.reset();
		}
	}

	// 换某一段的 clip：只重建它自己的 animator，别的不受影响。
	void SetClip(const std::string& name, std::shared_ptr<AnimationClip> newClip, const AssetGUID& guid)
	{
		auto it = animatorDatas.find(name);
		if (it == animatorDatas.end() || !it->second)
			return;

		it->second->clip = std::move(newClip);
		it->second->clipGuid = guid;

		if (it->second->clip && skeleton)
			it->second->animator = std::make_shared<Animator>(it->second->clip, skeleton);
		else
			it->second->animator.reset();
	}

	bool BuildAnimator(const std::string& name)
	{
		auto it = animatorDatas.find(name);
		if (it == animatorDatas.end() || !it->second || it->second->animator)
			return false;
		if (!skeleton || !it->second->clip)
			return false;
		it->second->animator = std::make_shared<Animator>(it->second->clip, skeleton);
		return true;
	}

	// 名字是 animatorDatas 的键，Default 引用的也是名字，所以默认动画要跟着一起改。
	bool RenameAnimator(const std::string& oldName, const std::string& newName)
	{
		if (oldName == newName)
			return true;
		if (oldName.empty() || newName.empty())
			return false;

		auto it = animatorDatas.find(oldName);
		if (it == animatorDatas.end())
			return false;
		// 重名不允许，否则两行动画会抢同一个键。
		if (animatorDatas.find(newName) != animatorDatas.end())
			return false;

		auto data = std::move(it->second);
		animatorDatas.erase(it);
		animatorDatas.emplace(newName, std::move(data));

		if (defaultAnimator == oldName)
			defaultAnimator = newName;
		if (idleAnimator == oldName)
			idleAnimator = newName;

		return true;
	}

	bool RemoveAnimator(const std::string& name)
	{
		if (animatorDatas.erase(name) == 0)
			return false;
		if (defaultAnimator == name)
			defaultAnimator.clear();
		if (idleAnimator == name)
			idleAnimator.clear();
		return true;
	}

	void ResetAnimatorGroup()
	{
		for (auto& [name, data] : animatorDatas)
		{
			data->enable = false;
			data->maxLoopCount = 0;
			data->curPlayTimeMs = 0;
		}
	}

	void PlayAnimator(const std::string& name, int maxLoopCount = 1)
	{
		auto it = animatorDatas.find(name);
		if (it == animatorDatas.end() || !it->second || !it->second->animator)
			return;
		it->second->enable = true;
		it->second->maxLoopCount = maxLoopCount;
		it->second->curPlayTimeMs = 0;
	}

	void StopAnimator(const std::string& name)
	{
		auto it = animatorDatas.find(name);
		if (it == animatorDatas.end() || !it->second)
			return;
		it->second->enable = false;
		it->second->maxLoopCount = 0;
		it->second->curPlayTimeMs = 0;
	}

	std::shared_ptr<SkeletonAnimatorData> GetAnimatorData(const std::string& name)
	{
		auto it = animatorDatas.find(name);
		if (it != animatorDatas.end())
			return it->second;
		return nullptr;
	}

	bool ExistAnimator(const std::string& name)
	{
		return animatorDatas.find(name) != animatorDatas.end();
	}

	bool HasAnimatorPlaying()
	{
		for (auto& [name, data] : animatorDatas)
		{
			if (data && data->enable)
				return true;
		}
		return false;
	}

private:
	void CloneFrom(const SkeletonAnimatorGroup& other)
	{
		skeletonGuid = other.skeletonGuid;
		skeleton = other.skeleton;
		defaultAnimator = other.defaultAnimator;
		idleAnimator = other.idleAnimator;

		animatorDatas.reserve(other.animatorDatas.size());

		for (const auto& [name, data] : other.animatorDatas)
		{
			if (!data)
				continue;

			auto copied = std::make_shared<SkeletonAnimatorData>(data->clipGuid);
			copied->clip = data->clip;
			copied->enable = data->enable;
			copied->maxLoopCount = data->maxLoopCount;
			copied->curPlayTimeMs = data->curPlayTimeMs;

			if (copied->clip && skeleton)
			{
				copied->animator = std::make_shared<Animator>(copied->clip, skeleton);
				copied->animator->SetTime(static_cast<float>(copied->curPlayTimeMs) / 1000.0f);
			}

			animatorDatas.emplace(name, std::move(copied));
		}
	}
};

HY_DECLARE_ECS_TYPE(SkeletonAnimatorGroup, "hy.engine.component.SkeletonAnimatorGroup");
