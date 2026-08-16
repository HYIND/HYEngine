#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECSCore/Entity.h"
#include "ECSCore/World.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "LightFactory.h"

class ParticleEmitterFactory
{
public:
	static Entity CreateFireEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateVortexEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Texture2D> texture);
};