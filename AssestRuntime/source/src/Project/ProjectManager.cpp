#include "Project/AssetAnalysisHelper.h"
#include "Project/ProjectManager.h"
#include "UI/UiDocumentParser.h"
#include "Helper/Tools.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

using json = nlohmann::json;

static const std::map<AssetType, std::string> metaPerfix = {
	{ AssetType::Texture,"Tex" },
	{ AssetType::StaticMesh,"StaticMesh" },
	{ AssetType::Material,"Mat" },
	{ AssetType::StaticModel,"StaticModel" },
	{ AssetType::Scene,"Scene" },
	{ AssetType::Audio,"Au" },
	{ AssetType::Script,"Script" },
	{ AssetType::UiDocument,"UI" },
	{ AssetType::UiStyle,"UIS" },
	{ AssetType::SkinnedMesh,"SkinnedMesh" },
	{ AssetType::SkinnedModel,"SkinnedModel" },
	{ AssetType::Skeleton,"Skeleton" },
	{ AssetType::AnimationClip,"AnimationClip" }
};

class ProjectAssetPathProvider :public IAssetFullPathProvider
{
public:
	ProjectAssetPathProvider(const std::string& projectPath)
		:m_projectPath(projectPath) {}
	virtual std::string GetFullPath(const AssetPath& assetPath) {
		return (m_projectPath / fs::path(assetPath)).string();
	}

private:
	fs::path m_projectPath;
};

// ============ GUID生成 ============
AssetGUID ProjectManager::GenerateGUID() {
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dis;

	std::stringstream ss;
	ss << std::hex << std::setfill('0');
	ss << std::setw(8) << dis(gen) << "-";
	ss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
	ss << std::setw(4) << ((dis(gen) & 0xFFFF) | 0x4000) << "-";
	ss << std::setw(4) << ((dis(gen) & 0xFFFF) | 0x8000) << "-";
	ss << std::setw(12) << dis(gen);
	return ss.str();
}

AssetMeta ProjectManager::CreateAssetMeta(AssetType type, const AssetPath& path)
{
	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = type;
	meta.path = GenerateAssetMetaPath(path, type);
	meta.name = fs::path(meta.path).filename().string();
	return meta;
}

std::string ProjectManager::GenerateAssetMetaPath(const AssetPath& pathStr, AssetType type)
{
	std::string perfix;
	auto it = metaPerfix.find(type);
	if (it != metaPerfix.end())
		perfix = it->second;

	if (perfix.empty()) perfix = "Asset";

	fs::path path(pathStr);
	std::string parentPathStr = path.has_parent_path() ? path.parent_path().string() : "";

	uint64_t count = 1;
	while (true)
	{
		AssetPath generateFileName = std::format("{}_{}_{}.meta", perfix, count, path.filename().string());

		fs::path generatePath = fs::path(parentPathStr) / fs::path(generateFileName);
		fs::path fullGeneratePath = fs::path(GetProjectAssetFullPath(generatePath.string()));
		if (!fs::exists(fullGeneratePath))
			return generatePath.string();
		count++;
	}

	return "";
}

ProjectManager::ProjectManager() {
	m_database = std::make_shared<AssetDatabase>();
	m_storage = std::make_shared<AssetStroage>();
	m_storage->SetMetadataProvider(m_database);
}

bool ProjectManager::CreateProject(const std::string& path, const std::string& name)
{
	fs::path projectFile = fs::path(path) / (name + ".project");
	if (fs::exists(projectFile)) return false;

	m_projectPath = path;
	m_projectName = name;
	m_defaultSceneGUID.clear();
	m_isOpen = true;

	m_database->Clear();		// 初始化数据库

	m_storage->ClearLoadedAsset();
	m_storage->SetPathProvider(std::make_shared<ProjectAssetPathProvider>(GetProjectFolderFullPath()));

	EnsureDirectories();		// 创建目录
	SaveProject();				// 保存项目

	return true;
}

bool ProjectManager::OpenProject(const std::string& filePath)
{
	if (!fs::exists(filePath)) return false;

	std::ifstream file(filePath);
	if (!file.is_open()) return false;

	json projectJson;
	try
	{
		file >> projectJson;
	}
	catch (const std::exception&)
	{
		return false;
	}

	if (projectJson.is_null())
		return false;
	if (!projectJson.is_object())
		return false;

	AssetGUID defaultSceneGUID;
	if (projectJson.contains("defaultScene") && projectJson["defaultScene"].is_string())
		defaultSceneGUID = projectJson["defaultScene"].get<std::string>();

	m_projectPath = fs::path(filePath).parent_path().string();
	m_projectName = fs::path(filePath).stem().string();
	m_defaultSceneGUID = std::move(defaultSceneGUID);
	m_isOpen = true;

	// 加载数据库
	m_database->Clear();
	if (!m_database->Load(projectJson["asset"]))
	{
		// 如果加载失败，用默认值
		EnsureDirectories();
	}

	m_storage->ClearLoadedAsset();
	m_storage->SetPathProvider(std::make_shared<ProjectAssetPathProvider>(GetProjectFolderFullPath()));

	return true;
}

bool ProjectManager::SaveProject()
{
	if (!m_isOpen)
		return false;
	json projectJson;
	projectJson["asset"] = m_database->Save();
	projectJson["defaultScene"] = m_defaultSceneGUID;

	auto filePath = GetProjectFileFullPath();
	std::ofstream file(filePath);
	if (!file.is_open()) return false;
	file << projectJson.dump();
	return true;
}

bool ProjectManager::CloseProject()
{
	m_isOpen = false;
	m_database->Clear();
	m_storage->ClearLoadedAsset();
	m_storage->SetPathProvider(nullptr);

	return true;
}

bool ProjectManager::IsProjectOpen() const
{
	return m_isOpen;
}

bool ProjectManager::SetDefaultSceneGUID(const AssetGUID& guid)
{
	if (!m_isOpen)
		return false;
	if (!guid.empty())
	{
		auto meta = m_database->GetAssetMetaByGuid(guid);
		if (!meta || meta->type != AssetType::Scene)
			return false;
	}
	m_defaultSceneGUID = guid;
	return SaveProject();
}

const AssetGUID& ProjectManager::GetDefaultSceneGUID() const
{
	return m_defaultSceneGUID;
}

std::shared_ptr<AssetDatabase> ProjectManager::GetDatabase()
{
	return m_database;
}

std::string ProjectManager::GetProjectFolderFullPath() const
{
	return m_projectPath;
}

std::string ProjectManager::GetProjectFileFullPath() const {
	return (fs::path(m_projectPath) / fs::path(m_projectName + ".project")).string();
}

std::string ProjectManager::GetProjectName() const
{
	return m_projectName;
}

std::string ProjectManager::GetProjectAssetFullPath(const AssetPath& assetPath) const
{
	return (fs::path(m_projectPath) / fs::path(assetPath)).string();
}

// ============ 导入资产 ============
bool ProjectManager::ImportStaticMeshAsset(const std::string& sourcePathStr, const std::string& destPathStr, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	// 复制到项目
	if (sourcePathStr != destPathStr && !CopyToProject(sourcePathStr, assetPath.string())) return false;

	AssetMeta meta = CreateAssetMeta(AssetType::StaticMesh, assetPath.string());

	StaticMeshAssetDescription desc;
	desc.SetPath({ assetPath.string() });
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	m_database->AddAssetMeta(meta);

	if (out) out->push_back(meta);

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::CreateSkinnedMeshAsset(const AssetPath& modelPath, std::vector<AssetMeta>* out)
{
	const AssetMeta meta = CreateAssetMeta(AssetType::SkinnedMesh, modelPath);
	SkinnedMeshAssetDescription desc;
	desc.SetPath({ modelPath });
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;
	if (!m_database->AddAssetMeta(meta))
		return false;
	if (out) out->push_back(meta);
	return true;
}

bool ProjectManager::ImportMeshAsset(const std::string& sourcePathStr, const std::string& destPathStr, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	auto analysis = AssetAnalysisHelper::AnalysisModelImportData(sourcePathStr);
	if (!analysis)
		return false;

	// 静态网格体始终产出，它顺带把模型文件拷进工程，蒙皮网格体共用这一份路径。
	std::vector<AssetMeta> staticMeshMetas;
	if (!ImportStaticMeshAsset(sourcePathStr, destPathStr, &staticMeshMetas, false))
		return false;
	const AssetMeta staticMeshMeta = staticMeshMetas.front();

	std::vector<AssetMeta> importedMetas;
	importedMetas.push_back(staticMeshMeta);

	if (analysis->hasSkeleton)
	{
		const AssetPath modelPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath)).string();

		std::vector<AssetMeta> skinnedMeshMetas;
		if (!CreateSkinnedMeshAsset(modelPath, &skinnedMeshMetas))
			return false;
		const AssetMeta skinnedMeshMeta = skinnedMeshMetas.front();
		importedMetas.push_back(skinnedMeshMeta);
	}

	if (out) *out = std::move(importedMetas);

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportModelAsset(const std::string& sourcePathStr, const std::string& destPathStr, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	auto analysis = AssetAnalysisHelper::AnalysisModelImportData(sourcePathStr);
	if (!analysis)
		return false;

	// 先导静态网格体：它顺带把模型文件拷进工程，后面所有资产描述共用这一份路径。
	std::vector<AssetMeta> staticMeshMetas;
	if (!ImportStaticMeshAsset(sourcePathStr, destPathStr, &staticMeshMetas, false))
		return false;
	const AssetMeta staticMeshMeta = staticMeshMetas.front();

	const AssetPath modelPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath)).string();

	// 一次导入产出的全部资产都往这里收：静态网格体、纹理、材质、模型、以及可能存在的蒙皮那一套。
	std::vector<AssetMeta> importedMetas;
	importedMetas.push_back(staticMeshMeta);

	fs::path sourceParentPath = fs::path(sourcePathStr).parent_path();
	fs::path destParentPath = fs::path(destPathStr).parent_path();

	// 生成纹理资产
	std::unordered_map<std::shared_ptr<AnalysisTextureAssetMeta>, AssetMeta> textureMetas;
	for (auto& textureAnalysisMeta : analysis->textureMetas)
	{
		fs::path sourceRelativePath = fs::relative(fs::path(textureAnalysisMeta->filepath), sourceParentPath);
		fs::path destPath = destParentPath / sourceRelativePath;
		std::vector<AssetMeta> metas;
		if (ImportTextureAsset(textureAnalysisMeta->filepath, destPath.string(), textureAnalysisMeta->config, &metas, false))
		{
			textureMetas[textureAnalysisMeta] = metas.front();
			importedMetas.push_back(metas.front());
		}
	}

	// 生成材质资产
	std::unordered_map<uint32_t, AssetGUID> materialGuids;
	for (uint32_t count = 0; auto& materialAnalysisMeta : analysis->materialMetas)
	{
		fs::path destPath = destParentPath / std::format("{}_mat{}", sourceParentPath.filename().string(), count);

		std::vector<AssetMeta> metas;
		std::map<TextureType, AssetGUID> dependency;
		for (auto& pair : materialAnalysisMeta->textures)
		{
			auto& type = pair.first;
			auto& tex = pair.second;
			auto it = textureMetas.find(tex);
			if (it != textureMetas.end())
				dependency[type] = it->second.guid;
		}

		if (ImportMaterialAsset(destPath.string(), materialAnalysisMeta->prop, dependency, &metas, false))
		{
			materialGuids[count] = metas.front().guid;
			importedMetas.push_back(metas.front());
		}
		else
			std::cout << "error!\n";

		count++;
	}

	const AssetMeta staticModelMeta = CreateAssetMeta(AssetType::StaticModel, modelPath);
	{
		StaticModelAssetDescription desc;
		desc.AddStaticMesh(staticMeshMeta.guid);
		desc.SetMaterial(materialGuids);
		if (!desc.SaveToFile(GetProjectAssetFullPath(staticModelMeta.path)))
			return false;
		if (!m_database->AddAssetMeta(staticModelMeta))
			return false;
	}
	importedMetas.push_back(staticModelMeta);

	if (analysis->hasSkeleton)
	{
		std::vector<AssetMeta> skinnedMeshMetas;
		if (!CreateSkinnedMeshAsset(modelPath, &skinnedMeshMetas))
			return false;
		const AssetMeta skinnedMeshMeta = skinnedMeshMetas.front();
		importedMetas.push_back(skinnedMeshMeta);

		const AssetMeta skinnedModelMeta = CreateAssetMeta(AssetType::SkinnedModel, modelPath);
		{
			SkinnedModelAssetDescription desc;
			desc.AddStaticMesh(skinnedMeshMeta.guid);
			desc.SetMaterial(materialGuids);
			if (!desc.SaveToFile(GetProjectAssetFullPath(skinnedModelMeta.path)))
				return false;
			if (!m_database->AddAssetMeta(skinnedModelMeta))
				return false;
		}
		importedMetas.push_back(skinnedModelMeta);

		const AssetMeta skeletonMeta = CreateAssetMeta(AssetType::Skeleton, modelPath);
		{
			SkeletonAssetDescription desc;
			desc.SetPath(modelPath);
			if (!desc.SaveToFile(GetProjectAssetFullPath(skeletonMeta.path)))
				return false;
			if (!m_database->AddAssetMeta(skeletonMeta))
				return false;
		}
		importedMetas.push_back(skeletonMeta);

		// 一个 fbx 可能有多段动画，每段产出一个独立资产。
		for (uint32_t index = 0; index < analysis->animationCount; ++index)
		{
			const AssetMeta clipMeta = CreateAssetMeta(AssetType::AnimationClip, modelPath);
			AnimationClipAssetDescription desc;
			desc.SetPath(modelPath);
			desc.SetIndex(static_cast<int>(index));
			if (!desc.SaveToFile(GetProjectAssetFullPath(clipMeta.path)))
				return false;
			if (!m_database->AddAssetMeta(clipMeta))
				return false;
			importedMetas.push_back(clipMeta);
		}
	}

	if (out) *out = std::move(importedMetas);

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportTextureAsset(const std::string& sourcePathStr, const std::string& destPathStr, const TextureConfig& config, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	if (sourcePathStr != destPathStr && !CopyToProject(sourcePathStr, assetPath.string())) return false;

	AssetMeta meta = CreateAssetMeta(AssetType::Texture, assetPath.string());

	TextureAssetDescription desc;
	desc.SetPath(assetPath.string());
	desc.SetConfig(config);
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	// 添加到数据库
	m_database->AddAssetMeta(meta);

	if (out) out->push_back(meta);

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportScriptAsset(const std::string& sourcePathStr, const std::string& destPathStr, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen || !fs::exists(sourcePathStr) || !fs::is_regular_file(sourcePathStr))
		return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath))
		return false;

	std::string extension = fs::path(sourcePathStr).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	if (extension != ".lua")
		return false;

	fs::path absoluteDestPath = fs::absolute(destPathStr).lexically_normal();
	fs::path projectPath = fs::absolute(m_projectPath).lexically_normal();
	fs::path assetPath = fs::relative(absoluteDestPath, projectPath);
	if (assetPath.empty() || assetPath.string().find("..") == 0)
		return false;

	if (auto existingMeta = m_database->GetAssetMetaByPath(assetPath.string()))
	{
		if (existingMeta->type != AssetType::Script)
			return false;
		if (out) out->push_back(*existingMeta);
		return true;
	}

	if (fs::exists(absoluteDestPath) && fs::weakly_canonical(sourcePathStr) != fs::weakly_canonical(absoluteDestPath.string()))
		return false;
	if (fs::weakly_canonical(sourcePathStr) != fs::weakly_canonical(absoluteDestPath.string()) &&
		!CopyToProject(sourcePathStr, assetPath.string()))
		return false;

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = AssetType::Script;
	meta.path = assetPath.string();
	meta.name = absoluteDestPath.filename().string();
	if (!m_database->AddAssetMeta(meta))
		return false;

	if (autoSave && !SaveProject())
	{
		m_database->RemoveAssetMeta(meta.guid);
		return false;
	}

	if (out) out->push_back(meta);
	return true;
}

bool ProjectManager::ImportTextAsset(const std::string& sourcePathStr, const std::string& destPathStr, AssetType type, const std::string& expectedExtension, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen || !fs::exists(sourcePathStr) || !fs::is_regular_file(sourcePathStr))
		return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath))
		return false;

	std::string extension = fs::path(sourcePathStr).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	if (extension != expectedExtension)
		return false;

	fs::path absoluteDestPath = fs::absolute(destPathStr).lexically_normal();
	fs::path projectPath = fs::absolute(m_projectPath).lexically_normal();
	fs::path assetPath = fs::relative(absoluteDestPath, projectPath);
	if (assetPath.empty() || assetPath.string().find("..") == 0)
		return false;

	if (auto existingMeta = m_database->GetAssetMetaByPath(assetPath.string()))
	{
		if (existingMeta->type != type)
			return false;
		if (out) out->push_back(*existingMeta);
		return true;
	}

	if (fs::exists(absoluteDestPath) && fs::weakly_canonical(sourcePathStr) != fs::weakly_canonical(absoluteDestPath.string()))
		return false;
	if (fs::weakly_canonical(sourcePathStr) != fs::weakly_canonical(absoluteDestPath.string()) &&
		!CopyToProject(sourcePathStr, assetPath.string()))
		return false;

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = type;
	meta.path = assetPath.string();
	meta.name = absoluteDestPath.filename().string();
	if (!m_database->AddAssetMeta(meta))
		return false;

	if (autoSave && !SaveProject())
	{
		m_database->RemoveAssetMeta(meta.guid);
		return false;
	}

	if (out) out->push_back(meta);
	return true;
}

bool ProjectManager::ImportUIDocumentAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out, bool autoSave)
{
	std::vector<AssetMeta> documentMetas;
	if (!ImportTextAsset(filePath, assetPath, AssetType::UiDocument, ".ui", &documentMetas, false))
		return false;

	// 文档引用的样式按“相对文档的路径”找：源文件旁边有就一起导入，
	// 并保持同样的相对路径，这样文档里的引用在工程内依然成立。
	const std::filesystem::path sourceDir = std::filesystem::path(filePath).parent_path();
	const std::filesystem::path destinationDir = std::filesystem::path(assetPath).parent_path();

	for (const std::string& reference : UI::CollectStyleReferences(filePath))
	{
		const std::filesystem::path relative = std::filesystem::path(reference).lexically_normal();
		if (relative.empty() || relative.is_absolute())
			continue;

		const std::filesystem::path styleSource = (sourceDir / relative).lexically_normal();
		if (!std::filesystem::exists(styleSource) || !std::filesystem::is_regular_file(styleSource))
			continue;

		const std::filesystem::path styleAssetPath = (destinationDir / relative).lexically_normal();
		const std::filesystem::path styleDestination = (std::filesystem::path(m_projectPath) / styleAssetPath).lexically_normal();

		ImportUIStyleAsset(styleSource.string(), styleDestination.string(), &documentMetas, false);
	}

	if (autoSave && !SaveProject())
		return false;

	if (out)
		out->insert(out->end(), documentMetas.begin(), documentMetas.end());
	return true;
}

bool ProjectManager::ImportUIStyleAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out, bool autoSave)
{
	return ImportTextAsset(filePath, assetPath, AssetType::UiStyle, ".uistyle", out, autoSave);
}

bool ProjectManager::ImportMaterialAsset(const std::string& destPathStr, const MaterialProperties& prop, const std::map<TextureType, AssetGUID>& dependencyTexture, std::vector<AssetMeta>* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	AssetMeta meta = CreateAssetMeta(AssetType::Material, assetPath.string());

	MaterialAssetDescription desc;
	desc.SetProperties(prop);
	desc.SetTexture(dependencyTexture);
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	// 添加到数据库
	m_database->AddAssetMeta(meta);

	if (out) out->push_back(meta);

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::DeleteAsset(const AssetGUID& guid)
{
	auto meta = m_database->GetAssetMetaByGuid(guid);
	if (!meta) return false;

	// 删除文件
	fs::path fullPath = GetProjectAssetFullPath(meta->path);
	if (fs::exists(fullPath))
		fs::remove(fullPath);

	m_database->RemoveAssetMeta(guid);
	SaveProject();
	return true;
}

bool ProjectManager::RenameAsset(const AssetGUID& guid, const AssetPath& newName)
{
	return false;
}

std::vector<AssetMeta> ProjectManager::GetAssetMetasInFolder(const AssetPath& folder)
{
	return m_database->GetAssetMetasInFolder(folder, GetProjectFolderFullPath());
}

void ProjectManager::DeleteAssetsInFolder(const fs::path& folderPath)
{
	DeleteAssetsInFolderPrivate(folderPath);
	SaveProject();
}

bool ProjectManager::CreateSceneAsset(const std::string& fullPath, const SceneCodec& scene, AssetMeta* out)
{
	if (!m_isOpen || fullPath.empty() || !Tool::IsSubDirectory(fullPath, m_projectPath))
		return false;

	try
	{
		fs::path absolutePath = fs::absolute(fullPath).lexically_normal();
		fs::path relativePath = fs::relative(absolutePath, fs::path(m_projectPath));
		if (relativePath.empty() || relativePath.string().find("..") == 0)
			return false;

		AssetPath assetPath = relativePath.string();
		std::string extension = absolutePath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		if (extension != ".scene")
			return false;

		bool isExist = false;
		std::shared_ptr<AssetMeta> meta;
		if (auto existingMeta = m_database->GetAssetMetaByPath(assetPath))
		{
			if (existingMeta->type != AssetType::Scene)
				return false;
			meta = existingMeta;
			isExist = true;
		}
		else
		{
			meta = std::make_shared<AssetMeta>();
			meta->guid = GenerateGUID();
			meta->path = std::move(assetPath);
			meta->name = absolutePath.filename().string();
			meta->type = AssetType::Scene;
			if (!m_database->AddAssetMeta(*meta))
				return false;
			isExist = false;
		}

		auto sceneAsset = std::make_shared<SceneAsset>();
		sceneAsset->SetScene(std::make_shared<SceneCodec>(scene));
		bool result = m_storage->SaveAsset(meta->guid, sceneAsset);
		if (!result) return false;

		if (!isExist)
		{
			m_database->MakeFolderAssetCacheDirty(meta->path);
			if (!SaveProject())
			{
				m_database->RemoveAssetMeta(meta->guid);
				return false;
			}
		}

		if (out) *out = *meta;
		return true;

	}
	catch (const fs::filesystem_error&)
	{
		return false;
	}
}

std::shared_ptr<AssetObject> ProjectManager::LoadAsset(AssetGUID guid)
{
	return m_storage->LoadAsset(guid);
}

bool ProjectManager::SaveAsset(AssetGUID guid)
{
	return m_storage->SaveAsset(guid);
}

std::vector<std::shared_ptr<ScriptSourceData>> ProjectManager::CheckScriptAssetChanges()
{
	if (!m_isOpen || !m_storage)
		return {};
	return m_storage->CheckScriptChanges();
}

std::vector<std::shared_ptr<UI::ParseResult>> ProjectManager::CheckUIDocumentChanges()
{
	if (!m_isOpen || !m_storage)
		return {};
	return m_storage->CheckUIDocumentChanges();
}

bool ProjectManager::CopyToProject(const std::string& filePath, const AssetPath& assetPath)
{
	fs::path fullAssetPath = fs::path(m_projectPath) / fs::path(assetPath);
	fs::path(fullAssetPath).parent_path().string();
	fs::create_directories(fs::path(fullAssetPath).parent_path());

	if (auto path = fs::path(filePath); path.extension().string() == ".gltf")
	{
		auto binSourcepath = path;
		auto binAssetPath = fs::path(assetPath);
		binSourcepath.replace_extension(".bin");
		binAssetPath.replace_extension(".bin");
		CopyToProject(binSourcepath.string(), binAssetPath.string());
	}

	try {
		fs::copy_file(filePath, fullAssetPath.string(), fs::copy_options::overwrite_existing);
		return true;
	}
	catch (...) {
		return false;
	}
}

void ProjectManager::EnsureDirectories() {
	fs::create_directories(fs::path(m_projectPath) / "Assets");
	fs::create_directories(fs::path(m_projectPath) / "Assets/Models");
	fs::create_directories(fs::path(m_projectPath) / "Assets/Textures");
	fs::create_directories(fs::path(m_projectPath) / "Assets/Materials");
	fs::create_directories(fs::path(m_projectPath) / "Assets/Scenes");
	fs::create_directories(fs::path(m_projectPath) / "Assets/Others");
}

void ProjectManager::DeleteAssetsInFolderPrivate(const fs::path& folderPath)
{
	if (!fs::exists(folderPath) || !fs::is_directory(folderPath))
		return;

	// 先递归处理子目录
	for (const auto& entry : fs::directory_iterator(folderPath))
	{
		if (entry.is_directory())
		{
			DeleteAssetsInFolderPrivate(entry.path());
		}
	}

	auto relativeFolder = fs::relative(
		folderPath,
		GetProjectFolderFullPath()
	);
	auto metas = GetAssetMetasInFolder(relativeFolder.string());
	for (auto& meta : metas)
		m_database->RemoveAssetMeta(meta.guid);
}
