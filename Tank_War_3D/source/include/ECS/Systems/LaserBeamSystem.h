#pragma once

#include "ECS/Core/System.h"
#include "ECS/Components/AllComponent.h"

class LaserBeamSystem :public System
{
public:
	LaserBeamSystem();
	virtual void update(float deltaTime) override;

private:
	void UpdateLaserBeamEmitter(Entity& entity, float deltaTime);
};