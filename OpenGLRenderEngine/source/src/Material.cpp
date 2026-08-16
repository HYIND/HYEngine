#pragma once

#include "Helper/Tools.h"
#include "OpenGLRenderEngine/Base/Material.h"
#include "OpenGLRenderEngine/General/IndirectDrawManager.h"
#include <algorithm>


MaterialData::MaterialData()
{
	static auto emptytex = std::make_shared<Texture2D>(1, 1);
	texture_albedo = emptytex->GetBindlessID();
	texture_metallic = emptytex->GetBindlessID();
	texture_roughness = emptytex->GetBindlessID();
	texture_ao = emptytex->GetBindlessID();
	texture_normal = emptytex->GetBindlessID();
	texture_emissive = emptytex->GetBindlessID();
	texture_metallicroughness = emptytex->GetBindlessID();
	texture_height = emptytex->GetBindlessID();
	texture_opacity = emptytex->GetBindlessID();
}

Material::Material()
{
	_uuid = Tool::GenerateSimpleUuid();
	needUpdateIndirectDraw = true;
}

Material::Material(const MaterialProperties& props)
	: _properties(props), _version(0)
{
	_uuid = Tool::GenerateSimpleUuid();
	needUpdateIndirectDraw = true;
}

Material::~Material()
{
	IndirectDrawManager::Instance()->deleteMaterial(*this);
}

void Material::SetTexture(TextureType type, std::shared_ptr<Texture2D> texture)
{
	_textures[type] = texture;
	SetChange();
}

// 设置属性
void Material::SetProperty(const MaterialProperties& props)
{
	_properties = props;
	SetChange();
}

void Material::SetAlbedo(const glm::vec3& albedo)
{
	if (albedo == _properties.albedo)
		return;
	_properties.albedo = albedo;
	SetChange();
}
void Material::SetMetallic(float metallic)
{
	if (_properties.metallic == metallic)
		return;
	_properties.metallic = std::clamp(metallic, 0.f, 1.f);
	SetChange();
}
void Material::SetRoughness(float roughness)
{
	if (_properties.roughness == roughness)
		return;
	_properties.roughness = std::clamp(roughness, 0.f, 1.f);
	SetChange();
}
void Material::SetAmbientOcclusion(float ambientOcclusion)
{
	if (_properties.ambientOcclusion == ambientOcclusion)
		return;
	_properties.ambientOcclusion = std::clamp(ambientOcclusion, 0.f, 1.f);
	SetChange();
}
void Material::SetOpacity(float opacity)
{
	if (_properties.opacity == opacity)
		return;
	_properties.opacity = std::clamp(opacity, 0.f, 1.f);
	SetChange();
}
void Material::SetIOR(float IOR)
{
	if (_properties.IOR == IOR)
		return;
	_properties.IOR = IOR;
	SetChange();
}

void Material::SetAlpahMode(AlphaMode mode)
{
	if (_properties.alphamode == mode)
		return;
	_properties.alphamode = mode;
	SetChange();
}

void Material::SetMaskThreshold(float value)
{
	if (_properties.maskthreshold == value)
		return;
	_properties.maskthreshold = std::clamp(value, 0.f, 1.f);
	SetChange();
}

void Material::SetTwoSided(bool value)
{
	if (_properties.twosided == value)
		return;
	_properties.twosided = value;
	SetChange();
}

// 获取属性
MaterialProperties Material::GetProperties() const
{
	return _properties;
}

glm::vec3 Material::GetAlbedo() const
{
	return _properties.albedo;
}

float Material::GetMetallic() const
{
	return _properties.metallic;
}

float Material::GetRoughness() const
{
	return _properties.roughness;
}

float Material::GetAmbientOcclusion() const
{
	return _properties.ambientOcclusion;
}

float Material::GetOpacity() const
{
	return _properties.opacity;
}

float Material::GetIOR() const
{
	return _properties.IOR;
}

AlphaMode Material::GetAlphaMode() const
{
	return _properties.alphamode;
}

float Material::GetMaskThreshold() const
{
	return _properties.maskthreshold;
}

bool Material::GetTwoSided() const
{
	return _properties.twosided;
}

uint32_t Material::GetVersion() const
{
	return _version.load();
}

const std::string& Material::GetUUID() const
{
	return _uuid;
}

const std::map<TextureType, std::shared_ptr<Texture2D>>& Material::GetTextures() const
{
	return _textures;
}

MaterialData Material::GetMaterialCompData()
{
	MaterialData comp_data;
	GetMaterialCompData(comp_data);
	return comp_data;
}

void Material::GetMaterialCompData(MaterialData& comp_data)
{
	auto& prop = _properties;

	comp_data.albedo = prop.albedo;
	comp_data.metallic = prop.metallic;
	comp_data.roughness = prop.roughness;
	comp_data.ambientOcclusion = prop.ambientOcclusion;
	comp_data.opacity = prop.opacity;
	comp_data.IOR = prop.IOR;
	comp_data.alphamode = prop.alphamode;
	comp_data.maskthreshold = prop.maskthreshold;
	comp_data.twosided = prop.twosided ? 1 : 0;

	auto& textures = _textures;
	for (auto& [type, tex] : textures)
	{
		if (!tex) continue;

		static auto writeTex = [](std::shared_ptr<Texture2D>& tex, GLuint64& socket, bool useFlag, int& count)-> void {
			if (!useFlag || count > 0) return;
			socket = tex->GetBindlessID();
			count++;
			};

		switch (type)
		{
		case TextureType::Albedo:
			writeTex(tex, comp_data.texture_albedo, prop.useAlbedoTexture, comp_data.texture_albedo_count);
			break;
		case TextureType::Metallic:
			writeTex(tex, comp_data.texture_metallic, prop.useMetallicTexture, comp_data.texture_metallic_count);
			break;
		case TextureType::Roughness:
			writeTex(tex, comp_data.texture_roughness, prop.useRoughnessTexture, comp_data.texture_roughness_count);
			break;
		case TextureType::Normal:
			writeTex(tex, comp_data.texture_normal, prop.useNormalTexture, comp_data.texture_normal_count);
			break;
		case TextureType::AO:
			writeTex(tex, comp_data.texture_ao, prop.useAOTexture, comp_data.texture_ao_count);
			break;
		case TextureType::MetallicRoughness:
			writeTex(tex, comp_data.texture_metallicroughness, prop.useMetallicRoughnessTexture, comp_data.texture_metallicroughness_count);
			break;
		case TextureType::Height:
			writeTex(tex, comp_data.texture_height, prop.useHeightTexture, comp_data.texture_height_count);
			break;
		case TextureType::Opacity:
			writeTex(tex, comp_data.texture_opacity, prop.useOpacityTexture, comp_data.texture_opacity_count);
		default:
			break;
		}

	}
}

std::shared_ptr<Material> Material::Clone() const
{
	auto m = std::make_shared<Material>(_properties);
	for (auto& [type, tex] : _textures)
		m->SetTexture(type, tex);
	return m;
}

void Material::SetNeedUpdateIndirectDraw(bool value) {
	needUpdateIndirectDraw = value; 
}

bool Material::GetNeedUpdateIndirectDraw() const{
	return needUpdateIndirectDraw;
}

void Material::SetChange() { 
	_version++;
	needUpdateIndirectDraw = true;
}

