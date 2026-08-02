#pragma once

#include "ECS/Core/System.h"

class MovementSystem :public System
{
public:
	virtual void update(float deltaTime) override;
};