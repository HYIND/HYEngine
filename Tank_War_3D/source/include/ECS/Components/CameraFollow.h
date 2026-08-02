#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECS/Core/IComponent.h"
#include "ECS/Core/Entity.h"

struct CameraFollow : public IComponent {
	Entity target;
	glm::vec3 offset = glm::vec3(-1.0f, 3.0f, -3.0f);
	float smoothSpeed = 5.0f;

	CameraFollow() {}
};