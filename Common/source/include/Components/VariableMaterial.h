#pragma once

#include "ECSCore/IComponent.h"
#include "CommonData/VariableMaterialData.h"
#include <algorithm>

struct VariableMaterial : public IComponent
{
	VariableMaterialData data;
	VariableMaterialChangeFlag flags;

	void SetAlbedo(const glm::vec3& albedo)
	{
		if (data.albedo == albedo)
			return;
		data.albedo = albedo;
		flags.albedoChange = true;
	}

	void SetMetallic(float metallic)
	{
		if (data.metallic == metallic)
			return;
		data.metallic = metallic;
		flags.metallicChange = true;
	}

	void SetRoughness(float roughness)
	{
		if (data.roughness == roughness)
			return;
		data.roughness = roughness;
		flags.roughnessChange = true;
	}

	void SetOpacity(float opacity)
	{
		if (data.opacity == opacity)
			return;
		data.opacity = std::clamp(opacity, 0.f, 1.f);
		flags.opacityChange = true;
	}

	void SetAlpahMode(VariableMaterialData::AlphaMode mode)
	{
		if (data.alphamode == mode)
			return;
		data.alphamode = mode;
		flags.alphamodeChange = true;
	}

	void SetTwoSide(bool twosided)
	{
		if (data.twosided == twosided)
			return;
		data.twosided = twosided;
		flags.twosidedChange = true;
	}

	void SetEmissionColor(const glm::vec3& emissionColor)
	{
		if (data.emissionColor == emissionColor)
			return;
		data.emissionColor = emissionColor;
		flags.emissionColorChange = true;
	}

	void SetEmissionStrength(float value)
	{
		if (data.emissionStrength == value)
			return;
		data.emissionStrength = value;
		flags.emissionStrengthChange = true;
	}
};