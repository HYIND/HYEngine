#pragma once

#include <memory>

enum class EffectType { Particle = 0, LaserBeam = 1 };
struct BaseEffectProperties
{
	EffectType effectType;
public:
	BaseEffectProperties() = default;
	virtual std::shared_ptr<BaseEffectProperties> Clone() = 0;
	virtual ~BaseEffectProperties() = default;
};
