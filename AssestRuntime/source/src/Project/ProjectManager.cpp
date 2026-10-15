#include "Project/AssetAnalysisHelper.h"
#include "Project/ProjectManager.h"
#include "Helper/Tools.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

using json = nlohmann::json;

static const std::map<AssetType, std::string> metaPerfix = {
	{ AssetType::Texture,"Tex" },
	{ AssetType::StaticMesh,"SM" },
	{ AssetType::Material,"Mat" },
	{ AssetType::Model,"Model" },
	{ AssetType::Scene,"Scene" },
	{ AssetType::Audio,"Au" }
};

class ProjectAssetPathProvider :public IAssetFullPathProvider
{
public:
	ProjectAssetPathProvider(const std::string& projectPath)
		:m_projectPath(projectPath) {
	}
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
		if (!fs::exists(generatePath))
			return generatePath.string();
		count++;
	}

	return "";
}

ProjectManager* ProjectManager::Get() {
	static ProjectManager* instance = new ProjectManager();
	return instance;
}

ProjectManager::ProjectManager() {
	m_database = std::make_shared<AssetDatabase>();
	m_loader = std::make_shared<AssetLoader>();
	m_loader->SetMetadataProvider(m_database);
}

bool ProjectManager::CreateProject(const std::string& path, const std::string& name)
{
	fs::path projectFile = fs::path(path) / (name + ".project");
	if (fs::exists(projectFile)) return false;

	m_projectPath = path;
	m_projectName = name;
	m_isOpen = true;

	m_database->Clear();		// 初始化数据库

	m_loader->ClearLoadedAsset();
	m_loader->SetPathProvider(std::make_shared<ProjectAssetPathProvider>(GetProjectFolderFullPath()));

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

	m_projectPath = fs::path(filePath).parent_path().string();
	m_projectName = fs::path(filePath).stem().string();
	m_isOpen = true;

	// 加载数据库
	m_database->Clear();
	if (!m_database->Load(projectJson["asset"]))
	{
		// 如果加载失败，用默认值
		EnsureDirectories();
	}

	m_loader->ClearLoadedAsset();
	m_loader->SetPathProvider(std::make_shared<ProjectAssetPathProvider>(GetProjectFolderFullPath()));

	return true;
}

bool ProjectManager::SaveProject()
{
	if (!m_isOpen)
		return false;
	json projectJson;
	projectJson["asset"] = m_database->Save();

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
	m_loader->ClearLoadedAsset();
	m_loader->SetPathProvider(nullptr);

	return true;
}

bool ProjectManager::IsProjectOpen() const
{
	return m_isOpen;
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
bool ProjectManager::ImportStaticMeshAsset(const std::string& sourcePathStr, const std::string& destPathStr, AssetMeta* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	// 复制到项目
	if (sourcePathStr != destPathStr && !CopyToProject(sourcePathStr, assetPath.string())) return false;

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = AssetType::StaticMesh;
	meta.path = GenerateAssetMetaPath(assetPath.string(), meta.type);
	meta.name = fs::path(meta.path).filename().string();

	StaticMeshAssetDescription desc;
	desc.SetPath({ assetPath.string() });
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	m_database->AddAssetMeta(meta);

	if (out) *out = meta;

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportModelAsset(const std::string& sourcePathStr, const std::string& destPathStr, AssetMeta* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	// 生成静态网格体资产
	AssetMeta staticMeshMeta;
	if (!ImportStaticMeshAsset(sourcePathStr, destPathStr, &staticMeshMeta, false))
		return false;

	auto modelImportMetaData = AssetAnalysisHelper::AnalysisModelImportData(sourcePathStr);

	fs::path sourceParentPath = fs::path(sourcePathStr).parent_path();
	fs::path destParentPath = fs::path(destPathStr).parent_path();

	// 生成纹理资产
	std::unordered_map<std::shared_ptr<AnalysisTextureAssetMeta>, AssetMeta> textureMetas;
	for (auto& textureAnalysisMeta : modelImportMetaData->textureMetas)
	{
		fs::path sourceRelativePath = fs::relative(fs::path(textureAnalysisMeta->filepath), sourceParentPath);
		fs::path destPath = destParentPath / sourceRelativePath;
		AssetMeta meta;
		if (ImportTextureAsset(textureAnalysisMeta->filepath, destPath.string(), textureAnalysisMeta->config, &meta, false))
			textureMetas[textureAnalysisMeta] = meta;
	}

	// 生成材质资产
	std::unordered_map<uint32_t, AssetGUID> materialGuids;
	for (uint32_t count = 0; auto& materialAnalysisMeta : modelImportMetaData->materialMetas)
	{
		fs::path destPath = destParentPath / std::format("{}_mat{}", sourceParentPath.filename().string(), count);

		AssetMeta meta;
		std::map<TextureType, AssetGUID> dependency;
		for (auto& pair : materialAnalysisMeta->textures)
		{
			auto& type = pair.first;
			auto& tex = pair.second;
			auto it = textureMetas.find(tex);
			if (it != textureMetas.end())
				dependency[type] = it->second.guid;
		}

		if (ImportMaterialAsset(destPath.string(), materialAnalysisMeta->prop, dependency, &meta, false))
			materialGuids[count] = meta.guid;
		else
			std::cout << "error!\n";

		count++;
	}

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = AssetType::Model;
	meta.path = GenerateAssetMetaPath(assetPath.string(), meta.type);
	meta.name = fs::path(meta.path).filename().string();

	// 填入静态网格体依赖和材质依赖
	ModelAssetDescription desc;
	desc.AddStaticMesh(staticMeshMeta.guid);
	desc.SetMaterial(materialGuids);
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	m_database->AddAssetMeta(meta);

	if (out) *out = meta;

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportTextureAsset(const std::string& sourcePathStr, const std::string& destPathStr, const TextureConfig& config, AssetMeta* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!fs::exists(sourcePathStr)) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	if (sourcePathStr != destPathStr && !CopyToProject(sourcePathStr, assetPath.string())) return false;

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = AssetType::Texture;
	meta.path = GenerateAssetMetaPath(assetPath.string(), meta.type);
	meta.name = fs::path(meta.path).filename().string();

	TextureAssetDescription desc;
	desc.SetPath(assetPath.string());
	desc.SetConfig(config);
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	// 添加到数据库
	m_database->AddAssetMeta(meta);

	if (out) *out = meta;

	if (autoSave) SaveProject();
	return true;
}

bool ProjectManager::ImportMaterialAsset(const std::string& destPathStr, const MaterialProperties& prop, const std::map<TextureType, AssetGUID>& dependencyTexture, AssetMeta* out, bool autoSave)
{
	if (!m_isOpen) return false;
	if (!Tool::IsSubDirectory(destPathStr, m_projectPath)) return false;

	fs::path assetPath = fs::relative(fs::path(destPathStr), fs::path(m_projectPath));

	AssetMeta meta;
	meta.guid = GenerateGUID();
	meta.type = AssetType::Material;
	meta.path = GenerateAssetMetaPath(assetPath.string(), meta.type);
	meta.name = fs::path(meta.path).filename().string();

	MaterialAssetDescription desc;
	desc.SetProperties(prop);
	desc.SetTexture(dependencyTexture);
	if (!desc.SaveToFile(GetProjectAssetFullPath(meta.path)))
		return false;

	// 添加到数据库
	m_database->AddAssetMeta(meta);

	if (out) *out = meta;

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

std::shared_ptr<AssetObject> ProjectManager::LoadAsset(AssetGUID guid)
{
	return m_loader->LoadAsset(guid);
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
