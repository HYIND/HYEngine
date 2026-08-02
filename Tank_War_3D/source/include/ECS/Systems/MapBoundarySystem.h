#pragma once

#include "ECS/Core/System.h"

class MapBoundarySystem :public System
{
public:
	void SetBoundaryHeight(float height);
	virtual void update(float deltaTime) override;

private:
	float boundaryHeight = - 60.f;
};