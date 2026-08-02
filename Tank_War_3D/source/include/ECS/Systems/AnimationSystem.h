#pragma once

#include "ECS/Core/System.h"
#include "ECS/Components/AllComponent.h"

class AnimationSystem :public System
{
public:
	virtual void postUpdate(float deltaTime) override;
};