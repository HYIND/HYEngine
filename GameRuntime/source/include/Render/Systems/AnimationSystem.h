#pragma once

#include "ECSCore/System.h"
#include "GameRuntimeComponents.h"

class AnimationSystem :public System
{
public:
	virtual void onAttach(World& world);
	virtual void postUpdate(float deltaTime) override;
};