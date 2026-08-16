#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "ECSCore/IComponent.h"

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

	void setMatrix(const glm::mat4& matrix)
	{
		glm::vec3 scale;
		glm::quat rotation;
		glm::vec3 position;
		glm::vec3 skew;
		glm::vec4 perspective;
		bool success = glm::decompose(matrix, scale, rotation, position, skew, perspective);
		if (success) {
			this->position = position;
			this->rotation = rotation;
			this->scale = scale;
		}
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