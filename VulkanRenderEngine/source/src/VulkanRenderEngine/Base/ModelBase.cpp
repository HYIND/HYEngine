#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ModelBase.h"
#include "VulkanRenderEngine/GlobalConfig.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>

#include "ThreadPool.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/AssimpGlmHelpers.h"
#include <filesystem>

namespace fs = std::filesystem;

static std::map<TextureType, TextureConfig> TextureConfigMap =
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

inline std::string GetExt(const std::string& path)
{
	return fs::path(path).extension().string();
}

void ModelBase::SetAssetGUID(const AssetGUID& guid)
{
	_assetGUID = guid;
}

const AssetGUID& ModelBase::GetAssetGUID() const
{
	return _assetGUID;
}

std::vector<std::shared_ptr<Texture2D>> ModelBase::LoadMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config)
{
	std::vector<std::shared_ptr<Texture2D>> textures;
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{

		aiString ai_str;
		mat->GetTexture(type, i, &ai_str);
		fs::path fs_all_path(std::string(ai_str.C_Str()));

		std::vector<fs::path> paths;
		paths.push_back(fs::path(_directory) / fs_all_path);
		if (fs_all_path.filename() != fs_all_path)
			paths.push_back(fs::path(_directory) / fs_all_path.filename());

		bool skip = false;
		for (auto& filepath : paths)
		{
			for (unsigned int j = 0; j < _textures_loaded.size(); j++)
			{
				if (_textures_loaded[j].path == filepath && _textures_loaded[j].config == config)
				{
					textures.push_back(_textures_loaded[j].tex);
					skip = true;
					break;
				}
			}
		}

		if (skip)
			continue;

		bool isLoadSuccess = false;
		for (auto& filepath : paths)
		{
			std::shared_ptr<Texture2D> tex = std::make_shared<Texture2D>(filepath.string(), config, true);
			if (tex->IsEmpty())
				continue;

			LoadedTexture texture;
			texture.tex = tex;
			texture.path = filepath.string();
			texture.config = config;
			textures.push_back(texture.tex);
			_textures_loaded.push_back(texture);
			isLoadSuccess = true;
			break;
		}
		if (!isLoadSuccess)
			std::cout << "Texture failed to load at path: " << fs_all_path.string() << std::endl;
	}
	return textures;
}
