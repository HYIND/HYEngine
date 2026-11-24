#include "Render/Systems/AnimationSystem.h"
#include "ECSCore/World.h"
#include "CommonEventDef.h"
#include "Render/Components/SkeletonPose.h"
#include "VulkanRenderEngine/Base/Skeleton.h"

#include <algorithm>
#include <execution>

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

namespace
{
	// 每个实体要算整副骨架，工作量远大于单个粒子，门槛设得低
	constexpr std::size_t kParallelThreshold = 2;

	// weak_ptr 和 shared_ptr 是不是同一个对象（按控制块比，不怕地址被复用）
	bool SameOwner(const std::weak_ptr<const void>& key, const std::shared_ptr<const void>& ptr)
	{
		return ptr && !key.expired() && !key.owner_before(ptr) && !ptr.owner_before(key);
	}

	bool PlayIdle(SkeletonAnimatorGroup& aniGroup, float deltaTime, const std::vector<glm::mat4>*& palette)
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
		palette = &idleData->animator->GetGlobalTransforms();
		return true;
	}

	// 只读写这个实体自己的组件 / Animator；Skeleton、AnimationClip 是共享的，只读。
	// 所以不同实体之间可以并行。
	void UpdateEntity(SkeletonAnimatorGroup& aniGroup, SkeletonPose& pose, float deltaTime)
	{
		bool isAnyAniPlaying = false;
		const std::vector<glm::mat4>* palette = nullptr;

		// 启用的 animator 都照常推进（以后做混合会用到），姿态暂取第一个
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
				palette = &ani_data->animator->GetGlobalTransforms();

			isAnyAniPlaying = true;
		}

		// 在播（或者在循环播 Idle）：每帧都变，照常拷
		if (isAnyAniPlaying || PlayIdle(aniGroup, deltaTime, palette))
		{
			pose.ClearStaticCache();
			if (palette)
				pose.globalTransforms = *palette;
			else
				pose.globalTransforms.clear();
			return;
		}

		// 没人在播、也没有 Idle：停在 Default 的第 0 帧，不推时间。
		// 同一个 animator 上一帧已经是这个姿态了，就不重建、不拷贝。
		// （换 clip / 换骨架都会新建 Animator，对象变了自然会重算）
		if (!aniGroup.defaultAnimator.empty())
		{
			auto data = aniGroup.GetAnimatorData(aniGroup.defaultAnimator);
			if (data && data->animator)
			{
				data->curPlayTimeMs = 0;
				const auto& transforms = data->animator->GetGlobalTransforms();
				if (pose.staticSource == SkeletonPose::StaticSource::DefaultFirstFrame
					&& SameOwner(pose.staticKey, data->animator)
					&& pose.globalTransforms.size() == transforms.size())
					return;

				data->animator->SetTime(0.f);
				pose.globalTransforms = data->animator->GetGlobalTransforms();
				pose.staticSource = SkeletonPose::StaticSource::DefaultFirstFrame;
				pose.staticKey = data->animator;
				return;
			}
		}

		// 没有 animator：用骨架的绑定姿态（沿父链累乘 bindLocal）作为全局变换。
		// 不能直接填单位阵——palette 还要乘各网格自己的逆绑定矩阵。只在换骨架时重算。
		if (aniGroup.skeleton && !aniGroup.skeleton->nodes.empty())
		{
			auto& nodes = aniGroup.skeleton->nodes;
			if (pose.staticSource == SkeletonPose::StaticSource::BindPose
				&& SameOwner(pose.staticKey, aniGroup.skeleton)
				&& pose.globalTransforms.size() == nodes.size())
				return;

			pose.globalTransforms.resize(nodes.size());
			for (std::size_t i = 0; i < nodes.size(); ++i)
			{
				const SkeletonNode& node = nodes[i];
				pose.globalTransforms[i] = (node.parentIndex == SkeletonNode::InvalidIndex)
					? node.bindLocalTransform
					: pose.globalTransforms[node.parentIndex] * node.bindLocalTransform;
			}
			pose.staticSource = SkeletonPose::StaticSource::BindPose;
			pose.staticKey = aniGroup.skeleton;
			return;
		}

		pose.ClearStaticCache();
		pose.globalTransforms.clear();
	}
}

void AnimationSystem::postUpdate(float deltaTime)
{
	auto entities = getWorld().getEntitiesWith<SkeletonAnimatorGroup>();

	// 串行：先把缺的 SkeletonPose 补上，再统一取组件指针。
	// 加组件会改组件存储（可能让之前取到的引用失效），不能放进并行段，也不能边加边取。
	for (auto& entity : entities)
	{
		if (!entity.hasComponent<SkeletonPose>())
			entity.addComponent<SkeletonPose>();
	}

	_items.clear();
	_items.reserve(entities.size());
	for (auto& entity : entities)
		_items.emplace_back(&entity.getComponent<SkeletonAnimatorGroup>(), &entity.getComponent<SkeletonPose>());

	// 并行：每个实体只碰自己的数据。用 par 不用 par_unseq：Animator 里有 vector 扩容（分配内存）
	auto update = [deltaTime](const std::pair<SkeletonAnimatorGroup*, SkeletonPose*>& item)
		{
			UpdateEntity(*item.first, *item.second, deltaTime);
		};
	if (_items.size() >= kParallelThreshold)
		std::for_each(std::execution::par, _items.begin(), _items.end(), update);
	else
		std::for_each(_items.begin(), _items.end(), update);

	if (auto* profile = getWorld().GetCurrentLogicProfile())
		profile->SetCounter("骨骼动画实体", static_cast<int64_t>(_items.size()));
}
