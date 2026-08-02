#pragma once

#include "OpenGLRenderEngine/Base/Material.h"
#include <algorithm>

inline Material::Material(const MaterialProperties& props)
	: properties(props)
{
}

void Material::SetTexture(TextureType type, std::shared_ptr<Texture2D> texture)
{
	textures[type] = texture;
}

// 设置属性
void Material::SetProperty(const MaterialProperties& props) { properties = props; }

void Material::SetAlbedo(const glm::vec3& albedo) {
	properties.albedo = albedo;
}
void Material::SetMetallic(float metallic) {
	properties.metallic = std::clamp(metallic, 0.f, 1.f);
}
void Material::SetRoughness(float roughness) {
	properties.roughness = std::clamp(roughness, 0.f, 1.f);
}
void Material::SetAmbientOcclusion(float ambientOcclusion) {
	properties.ambientOcclusion = std::clamp(ambientOcclusion, 0.f, 1.f);
}
void Material::SetOpacity(float opacity) {
	properties.opacity = std::clamp(opacity, 0.f, 1.f);
}
void Material::SetIOR(float IOR) {
	properties.IOR = IOR;
}

void Material::SetAlpahMode(AlphaMode mode)
{
	properties.alphamode = mode;
}

void Material::SetMaskThreshold(float value)
{
	properties.maskthreshold = std::clamp(value, 0.f, 1.f);
}

void Material::SetTwoSided(bool value)
{
	properties.twosided = value;
}

// 获取属性
MaterialProperties Material::GetProperties() const
{
	return properties; 
}

glm::vec3 Material::GetAlbedo() const
{
	return properties.albedo;
}

float Material::GetMetallic() const
{
	return properties.metallic;
}

float Material::GetRoughness() const
{
	return properties.roughness;
}

float Material::GetAmbientOcclusion() const
{
	return properties.ambientOcclusion;
}

float Material::GetOpacity() const
{
	return properties.opacity;
}

float Material::GetIOR() const
{
	return properties.IOR;
}

AlphaMode Material::GetAlphaMode() const
{
	return properties.alphamode;
}

float Material::GetMaskThreshold() const
{
	return properties.maskthreshold;
}

bool Material::GetTwoSided() const
{
	return properties.twosided;
}

const std::map<TextureType, std::shared_ptr<Texture2D>>& Material::GetTextures() const { return textures; }

std::shared_ptr<Material> Material::Clone() const
{
	auto m = std::make_shared<Material>(properties);
	for (auto& [type, tex] : textures)
		m->SetTexture(type, tex);
	return m;
}
