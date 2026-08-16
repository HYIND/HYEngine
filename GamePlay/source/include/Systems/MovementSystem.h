#pragma once

#include "ECSCore/System.h"

class MovementSystem :public System
{
public:
	virtual void update(float deltaTime) override;
};