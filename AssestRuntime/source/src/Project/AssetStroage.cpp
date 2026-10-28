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
	static std::shared_ptr<UIDocumentAsset> LoadUIDocument(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static std::shared_ptr<UIStyleAsset> LoadUIStyle(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);

public:
	static bool SaveAsset(std::shared_ptr<AssetObject> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);

public:
	static bool RefreshScriptIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, ScriptAsset& asset, std::shared_ptr<ScriptSourceData>* outData = nullptr);
	static bool RefreshUIStyleIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, UIStyleAsset& asset);
	// force 为真时忽略时间戳（例如引用的样式文件变了，文档的有效结果也变了）。
	static bool RefreshUIDocumentIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, UIDocumentAsset& asset, bool force, std::shared_ptr<UI::ParseResult>* outData = nullptr);

private:
	static bool SaveScene(std::shared_ptr<SceneAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static bool SaveMaterial(std::shared_ptr<MaterialAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
	static bool SaveScript(std::shared_ptr<ScriptAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider);
};

bool AssetStroageHelper::RefreshScriptIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, ScriptAsset& asset, std::shared_ptr<ScriptSourceData>* outData)
{
	if (!pathProvider || meta.type != AssetType::Script)
		return false;

	const std::string fullPath = pathProvider->GetFullPath(meta.path);
	std::error_code ec;
	auto writeTime = std::filesystem::last_write_time(fullPath, ec);
	if (ec)
		return false;	// 文件被删除或被占用：保留旧数据

	if (writeTime == asset._lastWriteTime)
		return false;

	ScriptAssetDescription desc;
	if (!desc.LoadFromFile(fullPath))
		return false;	// 读取失败不更新时间，下一轮重试

	auto oldData = asset.AsScript();
	asset._lastWriteTime = writeTime;
	if (!oldData || oldData->source != desc.GetSource())
	{
		auto data = std::make_shared<ScriptSourceData>();
		data->scriptGuid = meta.guid;
		data->source = std::move(desc.GetSource());
		asset._data = data;
	}

	if (outData)
		*outData = asset._data;

	return true;
}

bool AssetStroageHelper::RefreshUIStyleIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, UIStyleAsset& asset)
{
	if (!pathProvider || meta.type != AssetType::UiStyle)
		return false;

	const std::string fullPath = pathProvider->GetFullPath(meta.path);
	std::error_code ec;
	auto writeTime = std::filesystem::last_write_time(fullPath, ec);
	if (ec)
		return false;	// 文件被删除或被占用：保留旧数据

	if (writeTime == asset._lastWriteTime)
		return false;

	UI::ResolveContext context;
	context.projectRoot = pathProvider->GetFullPath(AssetPath());
	context.exists = [](const std::string& path) { return std::filesystem::exists(path); };

	auto style = std::make_shared<UI::UiStyle>();
	std::string error;
	if (!UI::ParseStyleFile(fullPath, context, *style, error))
		return false;	// 读取失败不更新时间，下一轮重试

	asset._lastWriteTime = writeTime;
	asset._style = std::move(style);
	return true;
}

bool AssetStroageHelper::RefreshUIDocumentIfChanged(const AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider, UIDocumentAsset& asset, bool force, std::shared_ptr<UI::ParseResult>* outData)
{
	if (!pathProvider || meta.type != AssetType::UiDocument)
		return false;

	const std::string fullPath = pathProvider->GetFullPath(meta.path);
	std::error_code ec;
	auto writeTime = std::filesystem::last_write_time(fullPath, ec);
	if (ec)
		return false;

	const bool fileChanged = (writeTime != asset._lastWriteTime);
	if (!fileChanged && !force)
		return false;

	UI::ResolveContext context;
	context.projectRoot = pathProvider->GetFullPath(AssetPath());
	context.exists = [](const std::string& path) { return std::filesystem::exists(path); };

	auto parsed = std::make_shared<UI::ParseResult>(UI::ParseDocumentFile(fullPath, context));
	if (!parsed->ok)
		return false;	// 解析失败：保留旧结果，等下一次文件变化

	asset._lastWriteTime = writeTime;
	asset._document = parsed;
	if (outData)
		*outData = std::move(parsed);
	return true;
}

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
	{
		if (result && result->GetType() == AssetType::Script)
		{
			auto metaProvider = _metaProvider;
			auto pathProvider = _pathProvider;
			if (metaProvider && pathProvider)
			{
				if (auto meta = metaProvider->GetAssetMetaByGuid(guid))
				{
					AssetStroageHelper::RefreshScriptIfChanged(*meta, pathProvider, static_cast<ScriptAsset&>(*result));
				}
			}
		}
		return result;
	}

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

std::vector<std::shared_ptr<ScriptSourceData>> AssetStroage::CheckScriptChanges()
{
	std::vector<std::shared_ptr<ScriptSourceData>> changed;
	for (auto& asset : m_GuidToAsset.GetValues())
	{
		if (!asset || asset->GetType() != AssetType::Script)
			continue;
		auto metaProvider = _metaProvider;
		auto pathProvider = _pathProvider;
		if (metaProvider && pathProvider)
		{
			if (auto meta = metaProvider->GetAssetMetaByGuid(asset->GetAssetGUID()))
			{
				std::shared_ptr<ScriptSourceData> data;
				if (AssetStroageHelper::RefreshScriptIfChanged(*meta, pathProvider, static_cast<ScriptAsset&>(*asset), &data))
					changed.push_back(std::move(data));
			}
		}
	}
	return changed;
}

std::vector<std::shared_ptr<UI::ParseResult>> AssetStroage::CheckUIDocumentChanges()
{
	std::vector<std::shared_ptr<UI::ParseResult>> changed;
	auto metaProvider = _metaProvider;
	auto pathProvider = _pathProvider;
	if (!metaProvider || !pathProvider)
		return changed;

	// 样式文件变化会影响引用它的文档，所以先扫样式，再据此强制重解析文档。
	bool styleChanged = false;
	for (auto& asset : m_GuidToAsset.GetValues())
	{
		if (!asset || asset->GetType() != AssetType::UiStyle)
			continue;
		if (auto meta = metaProvider->GetAssetMetaByGuid(asset->GetAssetGUID()))
		{
			if (AssetStroageHelper::RefreshUIStyleIfChanged(*meta, pathProvider, static_cast<UIStyleAsset&>(*asset)))
				styleChanged = true;
		}
	}

	for (auto& asset : m_GuidToAsset.GetValues())
	{
		if (!asset || asset->GetType() != AssetType::UiDocument)
			continue;
		if (auto meta = metaProvider->GetAssetMetaByGuid(asset->GetAssetGUID()))
		{
			std::shared_ptr<UI::ParseResult> data;
			if (AssetStroageHelper::RefreshUIDocumentIfChanged(*meta, pathProvider, static_cast<UIDocumentAsset&>(*asset), styleChanged, &data))
				changed.push_back(std::move(data));
		}
	}

	return changed;
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
	case AssetType::StaticModel:
		return LoadModel(meta, pathProvider);
	case AssetType::Scene:
		return LoadScene(meta, pathProvider);
	case AssetType::Audio:
		return LoadAudio(meta, pathProvider);
	case AssetType::Script:
		return LoadScript(meta, pathProvider);
	case AssetType::UiDocument:
		return LoadUIDocument(meta, pathProvider);
	case AssetType::UiStyle:
		return LoadUIStyle(meta, pathProvider);
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

	std::error_code ec;
	auto writeTime = std::filesystem::last_write_time(fullMetaPath, ec);
	if (!ec)
		asset->_lastWriteTime = writeTime;

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
		return SaveMaterial(std::static_pointer_cast<MaterialAsset>(asset), meta, pathProvider);
	case AssetType::StaticModel:
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

bool AssetStroageHelper::SaveMaterial(std::shared_ptr<MaterialAsset> asset, AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!asset || !pathProvider)
		return false;

	const std::string fullMetaPath = pathProvider->GetFullPath(meta.path);
	if (fullMetaPath.empty())
		return false;

	MaterialAssetDescription description;
	description.SetProperties(asset->GetProperties());
	description.SetTexture(asset->GetTextureDependencies());
	return description.SaveToFile(fullMetaPath);
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
	return desc.SaveToFile(fullMetaPath);
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
	bool result = desc.SaveToFile(fullMetaPath);

	if (result)
	{
		std::error_code ec;
		auto writeTime = std::filesystem::last_write_time(fullMetaPath, ec);
		if (!ec)
			asset->_lastWriteTime = writeTime;
	}

	return result;
}

std::shared_ptr<UIDocumentAsset> AssetStroageHelper::LoadUIDocument(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return nullptr;

	auto asset = std::make_shared<UIDocumentAsset>();

	UI::ResolveContext context;
	// 第一次以文档所在目录为基准，第二次以工程基路径为基准；两者都由解析器拼好，
	// 这里只负责判断"这个路径存不存在"。
	context.projectRoot = pathProvider->GetFullPath(AssetPath());
	context.exists = [](const std::string& path) { return std::filesystem::exists(path); };

	const std::string fullPath = pathProvider->GetFullPath(meta.path);
	auto parsed = std::make_shared<UI::ParseResult>(UI::ParseDocumentFile(fullPath, context));
	asset->_document = std::move(parsed);
	std::error_code timeEc;
	asset->_lastWriteTime = std::filesystem::last_write_time(fullPath, timeEc);
	return asset;
}

std::shared_ptr<UIStyleAsset> AssetStroageHelper::LoadUIStyle(AssetMeta& meta, std::shared_ptr<IAssetFullPathProvider> pathProvider)
{
	if (!pathProvider)
		return nullptr;

	auto asset = std::make_shared<UIStyleAsset>();

	UI::ResolveContext context;
	context.projectRoot = pathProvider->GetFullPath(AssetPath());
	context.exists = [](const std::string& path) { return std::filesystem::exists(path); };

	const std::string fullPath = pathProvider->GetFullPath(meta.path);
	auto style = std::make_shared<UI::UiStyle>();
	std::string error;
	if (UI::ParseStyleFile(fullPath, context, *style, error))
		asset->_style = std::move(style);
	std::error_code timeEc;
	asset->_lastWriteTime = std::filesystem::last_write_time(fullPath, timeEc);
	return asset;
}
