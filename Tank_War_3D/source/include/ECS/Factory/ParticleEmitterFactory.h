#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECS/Core/Entity.h"
#include "ECS/Core/World.h"
#include "OpenGLRenderEngine/Base/Model.h"

class ParticleEmitterFactory
{
public:
	static Entity CreateFireEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateVortexEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Texture2D> texture);
};