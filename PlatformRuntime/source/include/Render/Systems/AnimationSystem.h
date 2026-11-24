#pragma once

#include "ECSCore/System.h"
#include "PlatformRuntimeComponents.h"

#include <utility>
#include <vector>

struct SkeletonAnimatorGroup;
struct SkeletonPose;

class AnimationSystem :public System
{
public:
	virtual void onAttach(World& world);
	virtual void postUpdate(float deltaTime) override;

private:
	// 每帧复用：串行阶段取好的组件指针，并行阶段只碰这些
	std::vector<std::pair<SkeletonAnimatorGroup*, SkeletonPose*>> _items;
};

HY_DECLARE_ECS_TYPE(AnimationSystem, "hy.engine.system.Animation");
