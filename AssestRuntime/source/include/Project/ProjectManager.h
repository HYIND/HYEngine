#pragma once

#include "AssetDescription.h"
#include "AssetDatabase.h"
#include "AssetStroage.h"
#include "Script/ScriptSystemVM.h"

class ProjectManager
{
public:
	// 工程状态归引擎所有（EngineRuntime 持有唯一实例），宿主通过引擎拿指针，
	// 不再用全局单例，免得编辑器/游戏/服务器各自持有一份“当前工程”。
	ProjectManager();
	~ProjectManager() = default;
	ProjectManager(const ProjectManager&) = delete;
	ProjectManager& operator=(const ProjectManager&) = delete;

	// 项目操作
	bool CreateProject(const std::string& filePath, const std::string& name);
	bool OpenProject(const std::string& filePath);
	bool SaveProject();
	bool CloseProject();
	bool IsProjectOpen() const;
	bool SetDefaultSceneGUID(const AssetGUID& guid);
	const AssetGUID& GetDefaultSceneGUID() const;

	// 获取
	std::shared_ptr<AssetDatabase> GetDatabase();
	std::string GetProjectFolderFullPath() const;
	std::string GetProjectFileFullPath() const;
	std::string GetProjectName() const;
	std::string GetProjectAssetFullPath(const AssetPath& assetPath) const;

	// 便捷资产操作。导入一律往 out 里追加这次产出的资产，产出几个都行。
	bool ImportStaticMeshAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	// 网格体/模型导入会产出多个资产（例如有骨骼时同时产出静态与蒙皮），所以收集到 vector 里。
	bool ImportMeshAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	bool ImportModelAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	bool ImportTextureAsset(const std::string& filePath, const std::string& assetPath, const TextureConfig& config, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	// 动图：原文件拷进工程 + 导入配置；图集在加载时生成，不落盘
	bool ImportAnimatedImageAsset(const std::string& filePath, const std::string& assetPath, const TextureConfig& config, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	// Sprite：原图拷进工程 + 导入配置，区域之后在 Sprite 编辑器里编辑（没有区域时整张图就是一帧）
	bool ImportSpriteAsset(const std::string& filePath, const std::string& assetPath, const TextureConfig& config, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	// 同一张原图已经导入过的 type 资产（贴图 / 精灵）：元文件和原图在同一目录、名字是"前缀_编号_原图文件名.meta"，
	// 并且元文件里记的原图路径就是 sourcePath。转资产前用来提示重复
	std::vector<AssetMeta> FindImageAssetsFromSource(AssetType type, const AssetPath& sourcePath);
	bool ImportScriptAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	bool ImportUIDocumentAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);
	bool ImportUIStyleAsset(const std::string& filePath, const std::string& assetPath, std::vector<AssetMeta>* out = nullptr, bool autoSave = true);

	// .ui / .uistyle 这类"原样拷进工程"的文本资产共用这条导入逻辑。
	bool ImportTextAsset(const std::string& filePath, const std::string& assetPath, AssetType type, const std::string& expectedExtension, std::vector<AssetMeta>* out, bool autoSave);
	bool ImportMaterialAsset(const std::string& destPathStr, const MaterialProperties& prop, const std::map<TextureType, AssetGUID>& dependencyTexture, std::vector<AssetMeta>* out, bool autoSave = true);

	bool DeleteAsset(const AssetGUID& guid);
	bool RenameAsset(const AssetGUID& guid, const AssetPath& newAssetPath);

	std::vector<AssetMeta> GetAssetMetasInFolder(const AssetPath& folder);
	void DeleteAssetsInFolder(const fs::path& folderPath);

public:
	bool CreateSceneAsset(const std::string& fullPath, const SceneCodec& scene, AssetMeta* out = nullptr);

public:
	std::shared_ptr<AssetObject> LoadAsset(AssetGUID guid);
	bool SaveAsset(AssetGUID guid);

public:
	std::vector<std::shared_ptr<ScriptSourceData>> CheckScriptAssetChanges();
	std::vector<std::shared_ptr<UI::ParseResult>> CheckUIDocumentChanges();

private:
	std::string GenerateGUID();
	std::string GenerateAssetMetaPath(const AssetPath& path, AssetType type);
	AssetMeta CreateAssetMeta(AssetType type, const AssetPath& path);
	bool CreateSkinnedMeshAsset(const AssetPath& modelPath, std::vector<AssetMeta>* out = nullptr);
	bool CopyToProject(const std::string& filePathStr, const AssetPath& assetPath);
	void EnsureDirectories();
	void DeleteAssetsInFolderPrivate(const fs::path& folderPath);

private:
	std::shared_ptr<AssetDatabase> m_database;
	std::shared_ptr<AssetStroage> m_storage;

	std::string m_projectPath;
	std::string m_projectName;
	AssetGUID m_defaultSceneGUID;
	bool m_isOpen = false;
};
