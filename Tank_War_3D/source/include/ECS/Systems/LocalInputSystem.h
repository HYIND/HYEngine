#pragma once

#include "ECS/Core/System.h"

class LocalInputSystem :public System
{
public:
	virtual void preUpdate(float deltaTime) override;
};