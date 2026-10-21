#pragma once
#include <glm/glm.hpp>
#include <stdint.h>

namespace __LightDataDefault
{
	constexpr inline uint32_t __default_shadow_side = 1024;
	constexpr inline uint32_t __default_cascadeLevel = 4;
}

struct BaseLightData
{
	glm::vec3 color = glm::vec3(1.0f);

	bool volumeLightEnable = true;
	float volumetricScatteringIntensity = 1.0f;

	bool castShadow = true;
	uint32_t shadowMapSize = __LightDataDefault::__default_shadow_side;
};

struct DirectionalLightData : public BaseLightData
{
	float luxIntensity = 3.5f;

	uint32_t cascadeLevel = __LightDataDefault::__default_cascadeLevel;	//阴影级联
};

struct PointLightData : public BaseLightData
{
	float cdIntensity = 300.f;
};

struct SpotLightData : public BaseLightData
{
	float cdIntensity = 300.f;

	float cutOffAngle = 25.f;
	float outercutOffAngle = 45.f;
};

enum class LightType :uint32_t {
	Directional = 0,
	Point,
	Spot
};
