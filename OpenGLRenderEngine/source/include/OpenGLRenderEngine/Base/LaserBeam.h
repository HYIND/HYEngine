#pragma once

#include "OpenGLRenderEngine/Base/Effect.h"
#include "glm/gtc/matrix_transform.hpp"

struct LaserBeamProperties :public BaseEffectProperties
{
	glm::vec3 color;

	float white_width = 1.0f;
	float color_width = 1.0f;

	glm::vec3 start = glm::vec3(0.f);
	glm::vec3 end = glm::vec3(0.f);

public:
	LaserBeamProperties() { effectType = EffectType::LaserBeam; }
	LaserBeamProperties(const LaserBeamProperties& other)
		:color(other.color)
		, white_width(other.white_width)
		, color_width(other.color_width)
		, start(other.start)
		, end(other.end) {
		effectType = EffectType::LaserBeam;
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() override { return std::make_shared<LaserBeamProperties>(*this); };
};