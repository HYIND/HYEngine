#pragma once

#include "ECSCore/System.h"
#include "PlatformRuntimeComponents.h"

class AnimationSystem :public System
{
public:
	virtual void onAttach(World& world);
	virtual void postUpdate(float deltaTime) override;
};

HY_DECLARE_ECS_TYPE(AnimationSystem, "hy.engine.system.Animation");
