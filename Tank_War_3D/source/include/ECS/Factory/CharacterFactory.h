#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECS/Core/Entity.h"
#include "ECS/Core/World.h"

class CharacterFactory
{
public:
	static Entity CreatePlayerCharacter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model);
};