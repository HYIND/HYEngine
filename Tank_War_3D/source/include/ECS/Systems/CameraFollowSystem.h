#pragma once

#include "ECS/Core/System.h"
#include "ECS/Core/Entity.h"

class CameraFollowSystem : public System
{
public:
	virtual void postUpdate(float deltaTime) override;

private:
	void processCameraFollow(Entity& cameraEntity, Entity& targetEntity);
	void processFreeCmaera(Entity& cameraEntity, Entity& targetEntity);
};