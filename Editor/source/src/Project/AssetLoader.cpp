#include "Project/AssetDescription.h"
#include "Project/AssetLoader.h"
#include "Project/AssetAnalysisHelper.h"
#include <nlohmann/json.hpp>

class AssetLoadHelper
{
public:
	static std::shared_ptr<AssetObject> LoadAsset(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);

private:
	static std::shared_ptr<TextureAsset> LoadTexture(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<StaticMeshAsset> LoadStaticMesh(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<AudioAsset> LoadAudio(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<ModelAsset> LoadModel(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<MaterialAsset> LoadMaterial(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<SceneAsset> LoadScene(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
};


void AssetLoader::SetMetadataProvider(std::shared_ptr<IMetadataProvider> provider)
{
	_metaProvider = provider;
}

void AssetLoader::SetPathProvider(std::shared_ptr<IAssetFullPathProvider> provider)
{
	_pathProvider = provider;
}

std::shared_ptr<AssetObject> AssetLoader::LoadAsset(const AssetGUID& guid)
{
	std::shared_ptr<AssetObject> result;
	if (m_GuidToAsset.Find(guid, result))
		return result;

	auto metaProvider = _metaProvider;
	auto pathProvider = _pathProvider;

	if (metaProvider && pathProvider)
	{
		if (auto meta = metaProvider->GetAssetMetaByGuid(guid))
		{
			if (auto asset = AssetLoadHelper::LoadAsset(*meta, pathProvider))
			{
				auto guard = m_GuidToAsset.MakeLockGuard();
				if (m_GuidToAsset.Find(guid, result))
					return result;

				asset->_loader = shared_from_this();
				m_GuidToAsset[guid] = asset;
				result = asset;
			}
		}
	}

	return result;
}

void AssetLoader::ClearLoadedAsset() {
	m_GuidToAsset.Clear();
}

std::shared_ptr<AssetObject> AssetLoadHelper::LoadAsset(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return nullptr;

	std::string fullPath = pathProvider->GetFullPath(meta.path);
	if (fullPath.empty())
		return nullptr;

	switch (meta.type)
	{
	case AssetType::StaticMesh:
		return LoadStaticMesh(meta, pathProvider);
	case AssetType::Texture:
		return LoadTexture(meta, pathProvider);
	case AssetType::Material:
		return LoadMaterial(meta, pathProvider);
	case AssetType::Model:
		return LoadModel(meta, pathProvider);
	case AssetType::Scene:
		return LoadScene(meta, pathProvider);
	case AssetType::Audio:
		return LoadAudio(meta, pathProvider);
	default:
		return nullptr;
	}
	return nullptr;
}

std::shared_ptr<TextureAsset> AssetLoadHelper::LoadTexture(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	auto asset = std::make_shared<TextureAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	TextureAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	auto config = desc.GetConfig();
	asset->_texture = std::make_shared<Texture2D>(pathProvider->GetFullPath(desc.GetPath()), config.gammaCorrection);
	asset->_texture->SetFiltering(config.minFilter, config.magFilter)
		.SetWrapping(config.wrapS, config.wrapT)
		.SetAnisotropy(config.anisotropy);

	return asset;
}

std::shared_ptr<StaticMeshAsset> AssetLoadHelper::LoadStaticMesh(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	auto asset = std::make_shared<StaticMeshAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	StaticMeshAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	for (auto& path : desc.GetPath())
		asset->_meshes.append_range(AssetAnalysisHelper::LoadStaticMesh(pathProvider->GetFullPath(path)));

	return asset;
}

std::shared_ptr<AudioAsset> AssetLoadHelper::LoadAudio(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	return std::shared_ptr<AudioAsset>();
}

std::shared_ptr<ModelAsset> AssetLoadHelper::LoadModel(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	auto asset = std::make_shared<ModelAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	ModelAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	asset->_dependencyStaticMesh = desc.GetStaticMesh();
	asset->_dependencyMaterial = desc.GetMaterial();

	return asset;
}

std::shared_ptr<MaterialAsset> AssetLoadHelper::LoadMaterial(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	auto asset = std::make_shared<MaterialAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	MaterialAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	asset->_properties = desc.GetProperties();
	asset->_dependencyTexture = desc.GetTexture();

	return asset;
}

std::shared_ptr<SceneAsset> AssetLoadHelper::LoadScene(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{


	return std::shared_ptr<SceneAsset>();
}
