#include "Project/AssetDescription.h"
#include "Project/AssetStroage.h"
#include "Project/AssetAnalysisHelper.h"
#include <nlohmann/json.hpp>

class AssetStroageHelper
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
	static std::shared_ptr<ScriptAsset> LoadScript(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);

public:
	static bool SaveAsset(std::shared_ptr<AssetObject> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);

private:
	static bool SaveScene(std::shared_ptr<SceneAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static bool SaveScript(std::shared_ptr<ScriptAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
};


void AssetStroage::SetMetadataProvider(std::shared_ptr<IMetadataProvider> provider)
{
	_metaProvider = provider;
}

void AssetStroage::SetPathProvider(std::shared_ptr<IAssetFullPathProvider> provider)
{
	_pathProvider = provider;
}

std::shared_ptr<AssetObject> AssetStroage::LoadAsset(const AssetGUID& guid)
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
			if (auto asset = AssetStroageHelper::LoadAsset(*meta, pathProvider))
			{
				asset->_assetGUID = meta->guid;
				auto guard = m_GuidToAsset.MakeLockGuard();
				if (m_GuidToAsset.Find(guid, result))
					return result;

				asset->_storage = shared_from_this();
				m_GuidToAsset[guid] = asset;
				result = asset;
			}
		}
	}

	return result;
}

bool AssetStroage::SaveAsset(const AssetGUID& guid, std::shared_ptr<AssetObject> newAsset)
{
	auto metaProvider = _metaProvider;
	auto pathProvider = _pathProvider;

	if (!metaProvider || !pathProvider)
		return false;

	auto meta = metaProvider->GetAssetMetaByGuid(guid);
	if (!meta)
		return false;

	if (!newAsset)
	{
		std::shared_ptr<AssetObject> asset;
		if (!m_GuidToAsset.Find(guid, asset))
			return false;
		return AssetStroageHelper::SaveAsset(asset, *meta, pathProvider);
	}
	else
	{
		bool result = AssetStroageHelper::SaveAsset(newAsset, *meta, pathProvider);
		if (!result)
			return false;

		if (newAsset->_assetGUID != meta->guid)
			newAsset->_assetGUID = meta->guid;

		auto storage = newAsset->_storage.lock();
		if (!storage || storage != shared_from_this())
			newAsset->_storage = shared_from_this();

		m_GuidToAsset[guid] = newAsset;
		return true;
	}
}

void AssetStroage::ClearLoadedAsset() {
	m_GuidToAsset.Clear();
}

std::shared_ptr<AssetObject> AssetStroageHelper::LoadAsset(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
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
	case AssetType::Script:
		return LoadScript(meta, pathProvider);
	default:
		return nullptr;
	}
	return nullptr;
}

std::shared_ptr<TextureAsset> AssetStroageHelper::LoadTexture(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	auto asset = std::make_shared<TextureAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	TextureAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	auto config = desc.GetConfig();
	asset->_texture = std::make_shared<Texture2D>(pathProvider->GetFullPath(desc.GetPath()), config, true);
	asset->_texture->SetAssetGUID(meta.guid);

	return asset;
}

std::shared_ptr<StaticMeshAsset> AssetStroageHelper::LoadStaticMesh(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
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

std::shared_ptr<AudioAsset> AssetStroageHelper::LoadAudio(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	return std::shared_ptr<AudioAsset>();
}

std::shared_ptr<ModelAsset> AssetStroageHelper::LoadModel(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
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

std::shared_ptr<MaterialAsset> AssetStroageHelper::LoadMaterial(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
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

std::shared_ptr<SceneAsset> AssetStroageHelper::LoadScene(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return nullptr;

	auto asset = std::make_shared<SceneAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	SceneAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	auto scene = std::make_shared<SceneCodec>();
	if (!scene->LoadFromJson(desc.GetJsonData()))
		return asset;

	asset->_scene = std::move(scene);
	return asset;
}

std::shared_ptr<ScriptAsset> AssetStroageHelper::LoadScript(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return nullptr;

	auto asset = std::make_shared<ScriptAsset>();

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	ScriptAssetDescription desc;
	if (!desc.LoadFromFile(fullMetaPath))
		return asset;

	auto data = std::make_shared<ScriptSourceData>();
	data->scriptGuid = meta.guid;
	data->source = desc.GetSource();
	asset->SetData(data);
	return asset;
}

bool AssetStroageHelper::SaveAsset(std::shared_ptr<AssetObject> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!asset)
		return false;

	if (!pathProvider)
		return false;

	std::string fullPath = pathProvider->GetFullPath(meta.path);
	if (fullPath.empty())
		return false;

	if (meta.type != asset->_type)
		return false;

	switch (meta.type)
	{
	case AssetType::StaticMesh:
		return false;
	case AssetType::Texture:
		return false;
	case AssetType::Material:
		return false;
	case AssetType::Model:
		return false;
	case AssetType::Scene:
		return SaveScene(std::static_pointer_cast<SceneAsset>(asset), meta, pathProvider);
	case AssetType::Audio:
		return false;
	case AssetType::Script:
		return SaveScript(std::static_pointer_cast<ScriptAsset>(asset), meta, pathProvider);
	default:
		return false;
	}
	return false;
}

bool AssetStroageHelper::SaveScene(std::shared_ptr<SceneAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return false;

	auto scene = asset->AsScene();
	if (!scene)
		return false;

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	SceneAssetDescription desc;
	desc.SetJsonData(scene->SaveToJson());
	if (!desc.SaveToFile(fullMetaPath))
		return false;

	return true;
}

bool AssetStroageHelper::SaveScript(std::shared_ptr<ScriptAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return false;

	auto script = asset->AsScript();
	if (!script)
		return false;

	std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	ScriptAssetDescription desc;
	desc.SetSource(script->source);
	if (!desc.SaveToFile(fullMetaPath))
		return false;

	return true;
}
