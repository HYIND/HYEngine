#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECSCore/Entity.h"
#include "ECSCore/World.h"
#include "OpenGLRenderEngine/Base/Model.h"

class CharacterFactory
{
public:
	static Entity CreatePlayerCharacter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model);
};