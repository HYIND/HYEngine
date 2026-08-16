#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"

class LightFollowSystem : public System
{
public:
	virtual void postUpdate(float deltaTime) override;

private:
	void processLightFollow(Entity& lightEntity, Entity& targetEntity);
};