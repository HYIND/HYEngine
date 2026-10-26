#pragma once

#include "ECSCore/System.h"
#include "Render/Components/LaserBeamEmitter.h"

class LaserBeamSystem :public System
{
public:
	LaserBeamSystem();
	virtual void update(float deltaTime) override;

private:
	void UpdateLaserBeamEmitter(Entity& entity, float deltaTime);
};

HY_DECLARE_ECS_TYPE(LaserBeamSystem, "hy.engine.system.LaserBeam");
