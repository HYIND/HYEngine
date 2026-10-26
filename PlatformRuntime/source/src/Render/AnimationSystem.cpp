#pragma once

#include "Render/Systems/AnimationSystem.h"
#include "ECSCore/World.h"
#include "CommonEventDef.h"

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
			if (ani_data->renderView.matTripleBuffer)
			{
				auto& triMats = ani_data->renderView.matTripleBuffer->acquireWriteBuffer();
				auto& mats = ani_data->animator->GetFinalBoneMatrices();
				if (mats.size() != triMats.size())
					triMats.resize(mats.size());

				for (int i = 0; i < mats.size(); i++)
					triMats[i] = mats[i];

				ani_data->renderView.matTripleBuffer->submitWriteBuffer();
			}
			isAnyAniPlaying = true;
		}

		if (!isAnyAniPlaying && aniGroup.ExistAnimator("Static"))
		{
			auto ani_data = aniGroup.GetAnimatorData("Static");
			if (!ani_data)
				continue;

			ani_data->animator->SetTime(0.f);
			if (ani_data->renderView.matTripleBuffer)
			{
				auto& triMats = ani_data->renderView.matTripleBuffer->acquireWriteBuffer();
				auto& mats = ani_data->animator->GetFinalBoneMatrices();
				if (mats.size() != triMats.size())
					triMats.resize(mats.size());

				for (int i = 0; i < mats.size(); i++)
					triMats[i] = mats[i];

				ani_data->renderView.matTripleBuffer->submitWriteBuffer();
			}
		}
	}
}