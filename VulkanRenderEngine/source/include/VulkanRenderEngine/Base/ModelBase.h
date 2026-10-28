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
