#pragma once

#include "AssetMeta.h"
#include "BiDirectionalMap.h"
#include "SafeStl.h"
#include <unordered_set>
#include <string>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>

// ============ 资产数据库 ============
class AssetDatabase :public IMetadataProvider
{
public:

	// 查询
	virtual std::shared_ptr<AssetMeta> GetAssetMetaByGuid(const AssetGUID& guid);
	std::shared_ptr<AssetMeta> GetAssetMetaByPath(const AssetPath& path);
	std::vector<AssetMeta> GetAllAssetMetas();
	std::vector<AssetMeta> GetAssetMetasByType(AssetType type);
	std::vector<AssetMeta> GetAssetMetasInFolder(const AssetPath& folder, const std::string& projectFullPath);

	// 增删改
	bool AddAssetMeta(const AssetMeta& meta);
	bool RemoveAssetMeta(const AssetGUID& guid);
	bool UpdateAssetMeta(const AssetGUID& guid, const AssetMeta& meta);
	bool UpdateAssetPath(const AssetGUID& guid, const AssetPath& newPath);

	// 保存/加载
	nlohmann::json Save();
	bool Load(const nlohmann::json& assetJson);

	void MakeFolderAssetCacheDirty(const AssetPath& path);
public:
	void Clear();					// 清空
	size_t GetAssetCount() const;	// 统计

private:
	SafeUnorderedMap<AssetGUID, AssetMeta> m_GuidToMeta;		// guid -> meta
	SafeBiDirectionalMap<AssetGUID, AssetPath> m_GuidToPath;	// guid <-> path
	SafeUnorderedMap<AssetPath, std::vector<AssetMeta>> m_FolderAssetCache;
};