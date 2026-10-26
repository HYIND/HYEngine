#pragma once

#include "ECSCore/System.h"

class LifetimeSystem :public System
{
public:
	virtual void update(float dt) override;
	virtual void preUpdate(float dt) override;
};

HY_DECLARE_ECS_TYPE(LifetimeSystem, "hy.engine.system.Lifetime");
