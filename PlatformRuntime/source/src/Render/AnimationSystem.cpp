#pragma once

#include "Render/Systems/AnimationSystem.h"
#include "ECSCore/World.h"
#include "CommonEventDef.h"
#include "Render/Components/SkeletonPose.h"
#include "VulkanRenderEngine/Base/Skeleton.h"

void AnimationSystem::onAttach(World& world)
{
	world.Subscribe<AnimationSystem, QueryExistAnimatorGroupEvent>([&](QueryExistAnimatorGroupEvent& event)->void {
		event.isExist = event.entity && event.entity.hasComponent<SkeletonAnimatorGroup>();
		});

	world.Subscribe<AnimationSystem, QueryExistPlayingAnimatorEvent>([&](QueryExistPlayingAnimatorEvent& event)->void {
		if (!(event.entity && event.entity.hasComponent<SkeletonAnimatorGroup>()))
		{
			event.isExist = false;
			return;
		}
		auto& ani = event.entity.getComponent<SkeletonAnimatorGroup>();
		event.isExist = ani.HasAnimatorPlaying();
		});

	world.Subscribe<AnimationSystem, ResetAnimatorGroupEvent>([&](ResetAnimatorGroupEvent& event)->void {
		if (!(event.entity && event.entity.hasComponent<SkeletonAnimatorGroup>()))
			return;
		auto& ani = event.entity.getComponent<SkeletonAnimatorGroup>();
		ani.ResetAnimatorGroup();
		});

	world.Subscribe<AnimationSystem, PlayAnimatorEvent>([&](PlayAnimatorEvent& event)->void {
		if (!(event.entity && event.entity.hasComponent<SkeletonAnimatorGroup>()))
			return;
		auto& ani = event.entity.getComponent<SkeletonAnimatorGroup>();
		ani.PlayAnimator(event.animationName, event.loopCount);
		});
}

void AnimationSystem::postUpdate(float deltaTime)
{
	auto entities = getWorld().getEntitiesWith<SkeletonAnimatorGroup>();
	for (auto& entity : entities)
	{
		bool isAnyAniPlaying = false;
		const std::vector<glm::mat4>* palette = nullptr;

		auto& aniGroup = entity.getComponent<SkeletonAnimatorGroup>();
		for (auto& [name, ani_data] : aniGroup.animatorDatas)
		{
			if (!ani_data || !ani_data->animator || !ani_data->enable)
				continue;

			ani_data->curPlayTimeMs += deltaTime;

			if (ani_data->maxLoopCount > 0)
			{
				int loopCount = (float)ani_data->curPlayTimeMs / (ani_data->animator->GetDurationTime() * 1000.f);
				if (loopCount >= ani_data->maxLoopCount)
				{
					ani_data->enable = false;
					continue;
				}
			}

			ani_data->animator->SetTime((float)ani_data->curPlayTimeMs / 1000.f);
			if (!palette)
				palette = &ani_data->animator->GetFinalBoneMatrices();

			isAnyAniPlaying = true;
		}

		static auto PlayIdle = [](SkeletonAnimatorGroup& aniGroup, float deltaTime, const std::vector<glm::mat4>*& palette) -> bool
			{
				if (aniGroup.idleAnimator.empty())
					return false;

				auto idleData = aniGroup.GetAnimatorData(aniGroup.idleAnimator);
				if (!idleData || !idleData->animator)
					return false;

				const float duration = idleData->animator->GetDurationTime();
				idleData->curPlayTimeMs += deltaTime;

				// Idle 永远循环，不计 maxLoopCount；时间自己绕回。
				const int64_t loopMs = static_cast<int64_t>(duration * 1000.f);
				if (loopMs > 0)
					idleData->curPlayTimeMs %= loopMs;

				idleData->animator->SetTime(static_cast<float>(idleData->curPlayTimeMs) / 1000.f);
				palette = &idleData->animator->GetFinalBoneMatrices();
				return true;
			};

		static auto PlayDefault = [](SkeletonAnimatorGroup& aniGroup, float deltaTime, const std::vector<glm::mat4>*& palette) -> bool
			{
				if (aniGroup.defaultAnimator.empty())
					return false;

				auto ani_data = aniGroup.GetAnimatorData(aniGroup.defaultAnimator);
				if (!ani_data || !ani_data->animator)
					return false;
				 
				ani_data->curPlayTimeMs = 0;
				ani_data->animator->SetTime(0.f);
				palette = &ani_data->animator->GetFinalBoneMatrices();
				return true;
			};

		// 没人在播：有 Idle 就循环播 Idle；没有 Idle 就停在 Default 的起点。
		if (!isAnyAniPlaying)
		{
			if (!PlayIdle(aniGroup, deltaTime, palette))
			{
				// 没有 Idle：停在 Default 的第 0 帧，不推时间。
				PlayDefault(aniGroup, deltaTime, palette);
			}
		}

		auto& pose = entity.hasComponent<SkeletonPose>() ? entity.getComponent<SkeletonPose>() : entity.addComponent<SkeletonPose>();
		if (palette)
			pose.palette.matrices = *palette;
		else if (aniGroup.skeleton && !aniGroup.skeleton->nodes.empty())
			pose.palette.matrices.assign(aniGroup.skeleton->nodes.size(), glm::mat4(1.0f));
		else
			pose.palette.matrices.clear();

	}
}
