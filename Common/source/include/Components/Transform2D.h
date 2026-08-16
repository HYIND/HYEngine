#pragma once

#include "glm/glm.hpp"
#include "ECSCore/IComponent.h"

struct Transform2D :public IComponent
{
	glm::vec2 position;
	float rotation;
	glm::vec2 scale;

	Transform2D(glm::vec2 pos = glm::vec2(0, 0), float rot = 0.f, glm::vec2 scl = glm::vec2(1, 1))
		: position(pos), rotation(rot), scale(scl) {
	}
};