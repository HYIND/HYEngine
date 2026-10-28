#pragma once

#include "StaticMesh.h"
#include "Material.h"
#include "CommonData/AssetGUIDDef.h"

#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <fstream>
#include <sstream>
#include <vector>

inline std::map<TextureType, TextureConfig> TextureConfigMap =
{
	// ========== 颜色类贴图（必须三线性，开启AF） ==========
	{
		TextureType::Albedo,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			true
		}
	},
	{
		TextureType::Emissive,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			true
		}
	},

	{
		TextureType::Normal,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::Roughness,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::Metallic,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::MetallicRoughness,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::AO,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			false,
			false
		}
	},

	{
		TextureType::Height,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			false,
			false
		}
	},
	{
		TextureType::Opacity,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			false,
			false
		}
	}
};


class ModelBase
{
private:
	struct LoadedTexture {
		std::shared_ptr<Texture2D> tex;
		std::string path;
		TextureConfig config;
	};

public:
	void SetAssetGUID(const AssetGUID& guid);
	const AssetGUID& GetAssetGUID() const;

protected:
	std::vector<std::shared_ptr<Texture2D>> LoadMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config);

protected:
	std::vector<LoadedTexture> _textures_loaded;

	std::string _directory;
	std::string _filepath;
	AssetGUID _assetGUID;
};
