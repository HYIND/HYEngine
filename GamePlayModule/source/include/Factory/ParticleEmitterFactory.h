#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECSCore/Entity.h"
#include "ECSCore/World.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "CommonData/LightFactory.h"

class ParticleEmitterFactory
{
public:
	static Entity CreateFireEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateVortexEmitter(World& world, const glm::vec3& position, const glm::quat& rotation);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<StaticModel> model);
	static Entity CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Texture2D> texture);
};