#pragma once

#include "stdafx.h"
#include "glm/glm.hpp"
#include "ECSCore/Entity.h"
#include "ECSCore/World.h"

class LightFactory
{
public:
	static Entity CreateDirLight(
		World& world,
		const glm::vec3& direction,
		const glm::vec3& color = glm::vec3(1.0f),
		float luxIntensity = 3.5f,
		bool castShadow = true,
		uint32_t cascadeLevel = 4,
		uint32_t shadowMapWidth = 1024,
		uint32_t shadowMapHeight = 1024
	);

	static Entity CreateSpotLight(
		World& world,
		const glm::vec3 position,
		const glm::vec3& direction,
		float cdIntensity = 300.f,
		float cutOffAngle = 22.5f,
		float outercutOffAngle = 30.f,
		const glm::vec3& color = glm::vec3(1.0f),
		bool castShadow = true,
		uint32_t shadowMapWidth = 1024,
		uint32_t shadowMapHeight = 1024
	);

	static Entity CreatePointLight(
		World& world,
		const glm::vec3 position,
		float cdIntensity = 300.f,
		const glm::vec3& color = glm::vec3(1.0f),
		bool castShadow = true,
		uint32_t shadowMapWidth = 1024,
		uint32_t shadowMapHeight = 1024
	);
};