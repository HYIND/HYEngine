#pragma once
#include <glm/glm.hpp>
#include <stdint.h>

namespace __LightDataDefault
{
	constexpr inline uint32_t __default_shadow_side = 1024;
	constexpr inline uint32_t __default_cascadeLevel = 4;
	constexpr inline float __default_shadowDistance = 200.0f;
	constexpr inline float __default_cascadeSplitLambda = 0.75f;
	constexpr inline float __default_shadowNormalBias = 1.0f;
	constexpr inline float __default_shadowSlopeBias = 2.0f;
}

struct BaseLightData
{
	glm::vec3 color = glm::vec3(1.0f);

	bool volumeLightEnable = true;
	bool volumeLightEffectByAtmospher = false;
	float volumetricScatteringIntensity = 1.0f;

	bool castShadow = true;
	uint32_t shadowMapSize = __LightDataDefault::__default_shadow_side;
	// 阴影法线偏移（单位：阴影贴图 texel）。接收点沿几何法线外推 texel 世界尺寸 × 该值 × sinθ，
	// 主要解决掠射角的自阴影；texel 尺寸按级联 / 到光源距离自动换算
	float shadowNormalBias = __LightDataDefault::__default_shadowNormalBias;
	// 阴影斜率深度偏移（光栅化 depthBias 的 slopeFactor）。画阴影图时硬件按三角形深度斜率把深度推远，
	// ≥ PCF 半径 + 0.5 才能让 3×3 PCF 外圈共用中心参考深度而不出痘
	float shadowSlopeBias = __LightDataDefault::__default_shadowSlopeBias;
};

struct DirectionalLightData : public BaseLightData
{
	float luxIntensity = 3.5f;

	uint32_t cascadeLevel = __LightDataDefault::__default_cascadeLevel;	//阴影级联
	// 阴影距离（米）：级联只在 [相机近平面, min(相机远平面, 阴影距离)] 内切分，超出部分不算阴影（末尾 10% 渐隐）
	float shadowDistance = __LightDataDefault::__default_shadowDistance;
	// 级联切分比例：0 = 均匀切分，1 = 对数切分（近处更密）
	float cascadeSplitLambda = __LightDataDefault::__default_cascadeSplitLambda;
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
