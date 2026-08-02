#pragma once

#include "ECS/Core/System.h"
#include "ECS/Core/Entity.h"

class LightFollowSystem : public System
{
public:
	virtual void postUpdate(float deltaTime) override;

private:
	void processLightFollow(Entity& lightEntity, Entity& targetEntity);
};