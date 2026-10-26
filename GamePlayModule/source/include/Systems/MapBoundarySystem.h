#pragma once

#include "ECSCore/System.h"

class MapBoundarySystem :public System
{
public:
	void SetBoundaryHeight(float height);
	virtual void update(float deltaTime) override;

private:
	float boundaryHeight = - 60.f;
};

HY_DECLARE_ECS_TYPE(MapBoundarySystem, "hy.gameplay.system.MapBoundary");
