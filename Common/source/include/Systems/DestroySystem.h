#pragma once

#include "ECSCore/System.h"

class DestroySystem : public System
{
public:
	virtual void preUpdate(float deltaTime)override;
	virtual void update(float deltaTime) override;
	virtual void postUpdate(float deltaTime) override;

private:
	void processDestructions();
};

HY_DECLARE_ECS_TYPE(DestroySystem, "hy.engine.system.Destroy");
