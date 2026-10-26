#pragma once

#include "glm/glm.hpp"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

// Camera behavior shared by the editor and game, so it is an engine component.
struct CameraFollow : public IComponent
{
	Entity target;
	glm::vec3 offset = glm::vec3(-1.0f, 3.0f, -3.0f);
	float smoothSpeed = 5.0f;
};

HY_DECLARE_ECS_TYPE(CameraFollow, "gameplay.camera_follow");
