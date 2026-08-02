#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECS/Core/IComponent.h"

struct Transform : public IComponent
{
	glm::vec3 position;
	glm::quat rotation;
	glm::vec3 scale = glm::vec3(1.0f);

	Transform(
		const glm::vec3& pos = glm::vec3(0.0f),
		const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		const glm::vec3& scl = glm::vec3(1.0f)
	) : position(pos), rotation(rot), scale(scl) {
	}

	glm::mat4 getMatrix() const
	{
		return glm::translate(glm::mat4(1.0f), position)
			* glm::toMat4(rotation)
			* glm::scale(glm::mat4(1.0f), scale);
	}

	glm::vec3 getDirection() const
	{
		return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, 1.0f));
	}
};