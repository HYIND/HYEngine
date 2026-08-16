#pragma once

#include "Components/Transform.h"
#include "ECSCore/IComponent.h"
#include "glm/glm.hpp"

struct Movement :public IComponent
{
	glm::vec3 currentMoveDirection = glm::vec3(0.f);

	bool currentWantJump = false;
	bool canJump = true;

	Movement() {}

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