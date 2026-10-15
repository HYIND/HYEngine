#include <fstream>
#include <algorithm>
#include <execution>

#include "Project/AssetDataBase.h"
#include "Helper/Tools.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

std::shared_ptr<AssetMeta> AssetDatabase::GetAssetMetaByGuid(const AssetGUID& guid)
{
	AssetMeta meta;
	if (!m_GuidToMeta.Find(guid, meta))
		return nullptr;
	return std::make_shared<AssetMeta>(std::move(meta));
}

std::shared_ptr<AssetMeta> AssetDatabase::GetAssetMetaByPath(const AssetPath& path)
{
	AssetGUID guid;
	if (m_GuidToPath.FindByRight(path, guid))
		return GetAssetMetaByGuid(guid);
	return nullptr;
}

std::vector<AssetMeta> AssetDatabase::GetAllAssetMetas()
{
	std::vector<AssetMeta> result;
	result.reserve(m_GuidToMeta.Size());
	m_GuidToMeta.EnsureCall([&](auto& map)-> void {
		for (auto& pair : map)
			result.emplace_back(pair.second);
		});
	return result;
}

std::vector<AssetMeta> AssetDatabase::GetAssetMetasByType(AssetType type)
{
	std::vector<AssetMeta> result;
	m_GuidToMeta.EnsureCall([&](auto& map)-> void {
		for (auto& pair : map) {
			if (pair.second.type == type) {
				result.emplace_back(pair.second);
			}
		}
		});
	return result;
}

std::vector<AssetMeta> AssetDatabase::GetAssetMetasInFolder(const AssetPath& folder, const std::string& projectFullPath)
{
	std::vector<AssetMeta> result;
	if (m_FolderAssetCache.Find(folder, result))
		return result;

	fs::path folderPath(folder);

	m_GuidToMeta.EnsureCall([&](auto& map)-> void {
		std::mutex mtx;
		std::for_each(std::execution::par, map.begin(), map.end(),
			[&](const auto& pair) {
				if (Tool::IsImmediateSubDirectory(pair.second.path, folder, projectFullPath)) {
					std::lock_guard<std::mutex> lock(mtx);
					result.push_back(pair.second);
				}
			});

		});

	m_FolderAssetCache[folder] = result;
	return result;
}

bool AssetDatabase::AddAssetMeta(const AssetMeta& meta)
{
	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	if (m_GuidToMeta.Exist(meta.guid))
		return false;

	m_GuidToMeta[meta.guid] = meta;
	m_GuidToPath.InsertOrUpdate(meta.guid, meta.path);

	MakeFolderAssetCacheDirty(meta.path);

	return true;
}

bool AssetDatabase::RemoveAssetMeta(const AssetGUID& guid)
{
	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	if (!m_GuidToMeta.Exist(guid))
		return false;

	auto path = m_GuidToMeta[guid].path;

	m_GuidToMeta.Erase(guid);
	m_GuidToPath.EraseByLeft(guid);

	MakeFolderAssetCacheDirty(path);

	return true;
}

bool AssetDatabase::UpdateAssetMeta(const AssetGUID& guid, const AssetMeta& meta)
{
	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	if (!m_GuidToMeta.Exist(guid))
		return false;

	auto& oriMeta = m_GuidToMeta[guid];
	auto oriPath = oriMeta.path;

	oriMeta = meta;
	if (oriPath != meta.path)
	{
		m_GuidToPath.InsertOrUpdate(guid, meta.path);
		MakeFolderAssetCacheDirty(oriPath);
		MakeFolderAssetCacheDirty(meta.path);
	}

	return true;
}

bool AssetDatabase::UpdateAssetPath(const AssetGUID& guid, const AssetPath& newPath)
{
	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	if (!m_GuidToMeta.Exist(guid))
		return false;

	auto oriPath = m_GuidToMeta[guid].path;

	m_GuidToMeta[guid].path = newPath;
	m_GuidToPath.InsertOrUpdate(guid, newPath);

	MakeFolderAssetCacheDirty(oriPath);
	MakeFolderAssetCacheDirty(newPath);

	return true;
}

// ============ 保存/加载 ============
nlohmann::json AssetDatabase::Save()
{
	json j = json::object();
	j["assetmetas"] = json::array();

	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	m_GuidToMeta.EnsureCall([&](auto& map)->void {
		for (const auto& pair : map) {
			const auto& meta = pair.second;
			json a;
			a["guid"] = meta.guid;
			a["path"] = meta.path;
			a["name"] = meta.name;
			a["type"] = static_cast<int>(meta.type);
			j["assetmetas"].push_back(a);
		}
		});

	return j;
}

bool AssetDatabase::Load(const nlohmann::json& assetJson)
{
	auto guard1 = m_GuidToMeta.MakeLockGuard();
	auto guard2 = m_GuidToPath.MakeLockGuard();

	Clear();

	// 加载资产
	if (assetJson.contains("assetmetas")) {
		for (const auto& a : assetJson["assetmetas"]) {
			AssetMeta meta;
			meta.guid = a.value("guid", "");
			meta.path = a.value("path", "");
			meta.name = a.value("name", "");
			meta.type = static_cast<AssetType>(a.value("type", 0));
			if (!meta.guid.empty()) {
				m_GuidToMeta[meta.guid] = meta;
				m_GuidToPath.InsertOrUpdate(meta.guid, meta.path);
			}
		}
	}

	return true;
}

void AssetDatabase::MakeFolderAssetCacheDirty(const AssetPath& path)
{
	m_FolderAssetCache.Erase(fs::path(path).parent_path().string());
}

void AssetDatabase::Clear()
{
	m_GuidToMeta.Clear();
	m_GuidToPath.Clear();
	m_FolderAssetCache.Clear();
}

size_t AssetDatabase::GetAssetCount() const
{
	return m_GuidToMeta.Size();
}