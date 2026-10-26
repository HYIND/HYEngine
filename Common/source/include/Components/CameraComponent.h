#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECSCore/IComponent.h"
#include "CommonData/Camera.h"
#include "Transform.h"

struct CameraComponent : public IComponent
{
	glm::vec3 worldUp = glm::vec3(0, 1, 0);

	float fov = 75.0f;
	float nearPlane = 0.1f;
	float farPlane = 200.0f;

	bool isFirstPerson = true;

	CameraComponent() {};
};

HY_DECLARE_ECS_TYPE(CameraComponent, "engine.camera");
