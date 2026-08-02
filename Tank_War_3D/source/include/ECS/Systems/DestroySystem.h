#pragma once

#include "ECS/Core/System.h"

class DestroySystem : public System
{
public:
	virtual void preUpdate(float deltaTime)override;
	virtual void update(float deltaTime) override;
	virtual void postUpdate(float deltaTime) override;

private:
	void processDestructions();
};