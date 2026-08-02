#pragma once

#include "ECS/Core/System.h"

class LifetimeSystem :public System
{
public:
	virtual void update(float dt) override;
	virtual void preUpdate(float dt) override;
};