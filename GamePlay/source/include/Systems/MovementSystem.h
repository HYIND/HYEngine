#pragma once

#include "ECSCore/System.h"

class MovementSystem :public System
{
public:
	virtual void update(float deltaTime) override;
};

HY_DECLARE_ECS_TYPE(MovementSystem, "hy.gameplay.system.Movement");
