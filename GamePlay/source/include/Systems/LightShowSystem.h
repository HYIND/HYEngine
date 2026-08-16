#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"

class LightShowSystem :public System
{
public:
	virtual void update(float deltaTime) override;

private:
	bool isEntityLighting(Entity& entity, Entity& lightEntity);
};