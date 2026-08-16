#pragma once

#include "AssetDescription.h"
#include "AssetDatabase.h"
#include "AssetLoader.h"

class ProjectManager
{
public:
	static ProjectManager* Get();

	// 项目操作
	bool CreateProject(const std::string& filePath, const std::string& name);
	bool OpenProject(const std::string& filePath);
	bool SaveProject();
	bool CloseProject();
	bool IsProjectOpen() const;

	// 获取
	std::shared_ptr<AssetDatabase> GetDatabase();
	std::string GetProjectFolderFullPath() const;
	std::string GetProjectFileFullPath() const;
	std::string GetProjectName() const;
	std::string GetProjectAssetFullPath(const AssetPath& assetPath) const;

	// 便捷资产操作
	bool ImportStaticMeshAsset(const std::string& filePath, const std::string& assetPath, AssetMeta* out = nullptr, bool autoSave = true);
	bool ImportModelAsset(const std::string& filePath, const std::string& assetPath, AssetMeta* out = nullptr, bool autoSave = true);
	bool ImportTextureAsset(const std::string& filePath, const std::string& assetPath, const TextureConfig& config, AssetMeta* out = nullptr, bool autoSave = true);
	bool ImportMaterialAsset(const std::string& destPathStr, const MaterialProperties& prop, const std::map<TextureType, AssetGUID>& dependencyTexture, AssetMeta* out, bool autoSave = true);

	bool DeleteAsset(const AssetGUID& guid);
	bool RenameAsset(const AssetGUID& guid, const AssetPath& newAssetPath);

	std::vector<AssetMeta> GetAssetMetasInFolder(const AssetPath& folder);

public:
	std::shared_ptr<AssetObject> LoadAsset(AssetGUID guid);

private:
	std::string GenerateGUID();
	std::string GenerateAssetMetaPath(const AssetPath& path, AssetType type);
	bool CopyToProject(const std::string& filePathStr, const AssetPath& assetPath);
	void EnsureDirectories();

private:
	ProjectManager();

	std::shared_ptr<AssetDatabase> m_database;
	std::shared_ptr<AssetLoader> m_loader;

	std::string m_projectPath;
	std::string m_projectName;
	bool m_isOpen = false;
};