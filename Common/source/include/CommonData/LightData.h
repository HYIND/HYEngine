#pragma once
#include <glm/glm.hpp>
#include <stdint.h>

namespace __LightDataDefault
{
	constexpr inline uint32_t __default_shadow_side = 1024;
	constexpr inline uint32_t __default_cascadeLevel = 4;
}

// 纯数据结构，不包含任何渲染逻辑
struct DirectionalLightData
{
	glm::vec3 color = glm::vec3(1.0f);
	float luxIntensity = 3.5f;

	// 阴影参数
	bool castShadow = true;
	uint32_t cascadeLevel = __LightDataDefault::__default_cascadeLevel;	//阴影级联
	uint32_t shadowMapWidth = __LightDataDefault::__default_shadow_side;
	uint32_t shadowMapHeight = __LightDataDefault::__default_shadow_side;
};

struct PointLightData
{
	glm::vec3 color = glm::vec3(1.0f);
	float cdIntensity = 300.f;

	bool castShadow = true;
	uint32_t shadowMapWidth = __LightDataDefault::__default_shadow_side;
	uint32_t shadowMapHeight = __LightDataDefault::__default_shadow_side;
};

struct SpotLightData
{
	glm::vec3 color = glm::vec3(1.0f);
	float cdIntensity = 300.f;

	float cutOffAngle = 15.f;
	float outercutOffAngle = 30.f;

	bool castShadow = true;
	uint32_t shadowMapWidth = __LightDataDefault::__default_shadow_side;
	uint32_t shadowMapHeight = __LightDataDefault::__default_shadow_side;
};

enum class LightType {
	Directional,
	Point,
	Spot
};