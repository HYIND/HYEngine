#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"

class CameraFollowSystem : public System
{
public:
	virtual void postUpdate(float deltaTime) override;

private:
	void processCameraFollow(Entity& cameraEntity, Entity& targetEntity);
	void processFreeCmaera(Entity& cameraEntity, Entity& targetEntity);
};