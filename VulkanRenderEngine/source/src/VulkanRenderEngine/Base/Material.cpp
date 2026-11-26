#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"


Material::Material()
{
	_uuid = Tool::GenerateSimpleUuid();
	needUpdateIndirectDraw = true;
}

Material::Material(const MaterialProperties& props)
	: _properties(props)
{
	_uuid = Tool::GenerateSimpleUuid();
	needUpdateIndirectDraw = true;
}

Material::~Material()
{
	IndirectDrawManager::Instance()->RetireMaterial(*this);
}

void Material::SetTexture(TextureType type, std::shared_ptr<Texture2D> texture)
{
	{
		std::lock_guard lock(_mutex);
		_textures[type] = texture;
	}
	SetChange();
}

// 设置属性
void Material::SetProperty(const MaterialProperties& props)
{
	{
		std::lock_guard lock(_mutex);
		_properties = props;
	}
	SetChange();
}

void Material::SetAlbedo(const glm::vec3& albedo)
{
	{
		std::lock_guard lock(_mutex);
		if (albedo == _properties.albedo)
			return;
		_properties.albedo = albedo;
	}
	SetChange();
}
void Material::SetMetallic(float metallic)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.metallic == metallic)
			return;
		_properties.metallic = std::clamp(metallic, 0.f, 1.f);
	}
	SetChange();
}
void Material::SetRoughness(float roughness)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.roughness == roughness)
			return;
		_properties.roughness = std::clamp(roughness, 0.001f, 1.f);
	}
	SetChange();
}
void Material::SetAmbientOcclusion(float ambientOcclusion)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.ambientOcclusion == ambientOcclusion)
			return;
		_properties.ambientOcclusion = std::clamp(ambientOcclusion, 0.f, 1.f);
	}
	SetChange();
}
void Material::SetOpacity(float opacity)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.opacity == opacity)
			return;
		_properties.opacity = std::clamp(opacity, 0.f, 1.f);
	}
	SetChange();
}
void Material::SetIOR(float IOR)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.IOR == IOR)
			return;
		_properties.IOR = IOR;
	}
	SetChange();
}

void Material::SetAlpahMode(AlphaMode mode)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.alphamode == mode)
			return;
		_properties.alphamode = mode;
	}
	SetChange();
}

void Material::SetMaskThreshold(float value)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.maskthreshold == value)
			return;
		_properties.maskthreshold = std::clamp(value, 0.f, 1.f);
	}
	SetChange();
}

void Material::SetTwoSided(bool value)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.twosided == value)
			return;
		_properties.twosided = value;
	}
	SetChange();
}


void Material::SetEmissionColor(const glm::vec3& emissionColor)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.emissionColor == emissionColor)
			return;
		_properties.emissionColor = emissionColor;
	}
	SetChange();
}

void Material::SetEmissionStrength(float value)
{
	{
		std::lock_guard lock(_mutex);
		if (_properties.emissionStrength == value)
			return;
		_properties.emissionStrength = value;
	}
	SetChange();
}

// 获取属性
MaterialProperties Material::GetProperties() const
{
	std::lock_guard lock(_mutex);
	return _properties;
}

glm::vec3 Material::GetAlbedo() const
{
	std::lock_guard lock(_mutex);
	return _properties.albedo;
}

float Material::GetMetallic() const
{
	std::lock_guard lock(_mutex);
	return _properties.metallic;
}

float Material::GetRoughness() const
{
	std::lock_guard lock(_mutex);
	return _properties.roughness;
}

float Material::GetAmbientOcclusion() const
{
	std::lock_guard lock(_mutex);
	return _properties.ambientOcclusion;
}

float Material::GetOpacity() const
{
	std::lock_guard lock(_mutex);
	return _properties.opacity;
}

float Material::GetIOR() const
{
	std::lock_guard lock(_mutex);
	return _properties.IOR;
}

AlphaMode Material::GetAlphaMode() const
{
	std::lock_guard lock(_mutex);
	return _properties.alphamode;
}

float Material::GetMaskThreshold() const
{
	std::lock_guard lock(_mutex);
	return _properties.maskthreshold;
}

bool Material::GetTwoSided() const
{
	std::lock_guard lock(_mutex);
	return _properties.twosided;
}

glm::vec3 Material::GetEmissionColor() const
{
	std::lock_guard lock(_mutex);
	return _properties.emissionColor;
}

float Material::GetEmissionStrength() const
{
	std::lock_guard lock(_mutex);
	return _properties.emissionStrength;
}

uint32_t Material::GetVersion() const
{
	return _version.load();
}

const std::string& Material::GetUUID() const
{
	return _uuid;
}

void Material::SetAssetGUID(const AssetGUID& guid)
{
	_assetGUID = guid;
}

const AssetGUID& Material::GetAssetGUID() const
{
	return _assetGUID;
}

std::map<TextureType, std::shared_ptr<Texture2D>> Material::GetTextures() const
{
	std::lock_guard lock(_mutex);
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
	MaterialProperties prop;
	std::map<TextureType, std::shared_ptr<Texture2D>> textures;
	{
		std::lock_guard lock(_mutex);
		prop = _properties;
		textures = _textures;
	}

	comp_data.albedo = prop.albedo;
	comp_data.metallic = prop.metallic;
	comp_data.roughness = prop.roughness;
	comp_data.ambientOcclusion = prop.ambientOcclusion;
	comp_data.emissionColor = prop.emissionColor;
	comp_data.emissionStrength = prop.emissionStrength;
	comp_data.opacity = prop.opacity;
	comp_data.IOR = prop.IOR;
	comp_data.alphamode = prop.alphamode;
	comp_data.maskthreshold = prop.maskthreshold;
	comp_data.twosided = prop.twosided ? 1 : 0;

	for (auto& [type, tex] : textures)
	{
		if (!tex) continue;

		static auto writeTex = [](std::shared_ptr<Texture2D>& tex, uint32_t& socket, uint32_t& count)-> void {
			if (count > 0)
				return;
			const BindlessIndex index = BindlessTextureManager::Instance()->GetTextureIndex(tex);
			if (index == BindlessIndexNull)
				return;
			socket = index;
			count++;
			};

		switch (type)
		{
		case TextureType::Albedo:
			writeTex(tex, comp_data.texture_albedo, comp_data.texture_albedo_count);
			break;
		case TextureType::Metallic:
			writeTex(tex, comp_data.texture_metallic, comp_data.texture_metallic_count);
			break;
		case TextureType::Roughness:
			writeTex(tex, comp_data.texture_roughness, comp_data.texture_roughness_count);
			break;
		case TextureType::Normal:
			writeTex(tex, comp_data.texture_normal, comp_data.texture_normal_count);
			break;
		case TextureType::AO:
			writeTex(tex, comp_data.texture_ao, comp_data.texture_ao_count);
			break;
		case TextureType::Emissive:
			writeTex(tex, comp_data.texture_emissive, comp_data.texture_emissive_count);
			break;
		case TextureType::MetallicRoughness:
			writeTex(tex, comp_data.texture_metallicroughness, comp_data.texture_metallicroughness_count);
			break;
		case TextureType::Height:
			writeTex(tex, comp_data.texture_height, comp_data.texture_height_count);
			break;
		case TextureType::Opacity:
			writeTex(tex, comp_data.texture_opacity, comp_data.texture_opacity_count);
		default:
			break;
		}

	}
}

ShadowMaterialData Material::GetShadowMaterialCompData()
{
	ShadowMaterialData comp_data;
	GetShadowMaterialCompData(comp_data);
	return comp_data;
}

void Material::GetShadowMaterialCompData(ShadowMaterialData& comp_data)
{
	MaterialProperties prop;
	std::map<TextureType, std::shared_ptr<Texture2D>> textures;
	{
		std::lock_guard lock(_mutex);
		prop = _properties;
		textures = _textures;
	}

	comp_data.opacity = prop.opacity;
	comp_data.alphamode = prop.alphamode;
	comp_data.maskthreshold = prop.maskthreshold;

	for (auto& [type, tex] : textures)
	{
		if (!tex) continue;

		static auto writeTex = [](std::shared_ptr<Texture2D>& tex, uint32_t& socket, uint32_t& count)-> void {
			if (count > 0)
				return;
			const BindlessIndex index = BindlessTextureManager::Instance()->GetTextureIndex(tex);
			if (index == BindlessIndexNull)
				return;
			socket = index;
			count++;
			};

		switch (type)
		{
		case TextureType::Albedo:
			writeTex(tex, comp_data.texture_albedo, comp_data.texture_albedo_count);
			break;
		case TextureType::Opacity:
			writeTex(tex, comp_data.texture_opacity, comp_data.texture_opacity_count);
		default:
			break;
		}

	}
}

std::shared_ptr<Material> Material::Clone() const
{
	MaterialProperties properties;
	std::map<TextureType, std::shared_ptr<Texture2D>> textures;
	{
		std::lock_guard lock(_mutex);
		properties = _properties;
		textures = _textures;
	}
	auto m = std::make_shared<Material>(properties);
	m->_assetGUID = _assetGUID;
	for (auto& [type, tex] : textures)
		m->SetTexture(type, tex);
	return m;
}

void Material::SetNeedUpdateIndirectDraw(bool value) {
	needUpdateIndirectDraw = value;
}

bool Material::GetNeedUpdateIndirectDraw() const {
	return needUpdateIndirectDraw;
}

void Material::SetChange() {
	_version++;
	needUpdateIndirectDraw = true;
}

