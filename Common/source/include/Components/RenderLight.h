#pragma once

#include "CommonData/LightData.h"
#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"
#include "Renderable.h"
#include <variant>

struct RenderLight : public Renderable 
{
	LightType type = LightType::Directional;
	bool renderCube = false;

	std::variant<DirectionalLightData, PointLightData, SpotLightData> data;

	RenderLight() = default;
	RenderLight(const DirectionalLightData& dirData)
		: type(LightType::Directional), data(dirData) {
	}
	RenderLight(const PointLightData& pointData)
		: type(LightType::Point), data(pointData) {
	}
	RenderLight(const SpotLightData& spotData)
		: type(LightType::Spot), data(spotData) {
	}

	template<typename T>
	T* GetData() {
		return std::get_if<T>(&data);
	}

	template<typename T>
	const T* GetData() const {
		return std::get_if<T>(&data);
	}
};
