#pragma once

#include "ECS/Components/Transform.h"
#include <algorithm>
#include "ECS/Core/IComponent.h"
#include "glm/glm.hpp"

struct CharacterMovement :public IComponent
{
	glm::vec3 currentMoveDirection = glm::vec3(0.f);

	bool currentWantJump = false;
	bool canJump = true;

	CharacterMovement() {}

	void setCurrentMoveDirection(const glm::vec3& dir)
	{
		if (dir.x != 0 || dir.y != 0 || dir.z != 0)
			currentMoveDirection = glm::normalize(dir);
		else
			currentMoveDirection = dir;
	}
	void SetCurrentJump(bool value)
	{
		if (canJump)
			currentWantJump = value;
		else
			currentWantJump = false;
	}
};