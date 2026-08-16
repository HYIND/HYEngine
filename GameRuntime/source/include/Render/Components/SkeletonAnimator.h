#pragma once

#include "OpenGLRenderEngine/Base/Animator.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

struct SkeletonAnimatorGroup :public IComponent
{
	struct SkeletonAnimatorData
	{
		std::shared_ptr<Animator> animator;
		OpenGLRenderContext::AnimatorView renderView;

		bool enable = false;
		int maxLoopCount = 0;
		int64_t curPlayTimeMs = 0;

		SkeletonAnimatorData(std::shared_ptr<Animator>& animator, bool enable = false)
			:animator(animator), enable(enable) {
		}
	};

	std::map<std::string, std::shared_ptr<SkeletonAnimatorData>> animatorDatas;

	SkeletonAnimatorGroup() {}

	bool AddAnimator(const std::string& name, std::shared_ptr<Animation> ani, std::shared_ptr<Skeleton> skeleton, bool enable = false)
	{
		if (!ani || !skeleton)
			return false;
		if (animatorDatas.find(name) != animatorDatas.end())
			return false;
		auto animator = std::make_shared<Animator>(ani, skeleton);
		animatorDatas[name] = std::make_shared<SkeletonAnimatorData>(animator, enable);
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
		if (it == animatorDatas.end())
			return;
		it->second->enable = true;
		it->second->maxLoopCount = maxLoopCount;
		it->second->curPlayTimeMs = 0;
	}

	void StopAnimator(const std::string& name)
	{
		auto it = animatorDatas.find(name);
		if (it == animatorDatas.end())
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
			if (data->enable)
				return true;
		}
		return false;
	}
};
