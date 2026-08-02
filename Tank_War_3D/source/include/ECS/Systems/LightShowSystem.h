#pragma once

#include "ECS/Core/System.h"
#include "ECS/Core/Entity.h"

class LightShowSystem :public System
{
public:
	virtual void update(float deltaTime) override;

private:
	bool isEntityLighting(Entity& entity, Entity& lightEntity);
};