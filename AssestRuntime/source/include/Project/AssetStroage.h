#pragma once

#include "AssetMeta.h"
#include "AssetObject.h"
#include "AssetLoader.h"
#include "SafeStl.h"

#include <atomic>

class IAssetFullPathProvider
{
public:
	virtual ~IAssetFullPathProvider() = default;
	virtual std::string GetFullPath(const AssetPath& assetPath) = 0;
};

class AssetStroage :public std::enable_shared_from_this<AssetStroage>
{
public:
	using AssetRequest = std::shared_ptr<AssetLoadTask<std::shared_ptr<AssetObject>>>;

	AssetStroage();
	~AssetStroage();

	void SetMetadataProvider(std::shared_ptr<IMetadataProvider> provider);
	void SetPathProvider(std::shared_ptr<IAssetFullPathProvider> provider);

	// 同步加载：已加载的直接返回；有人正在加载就等它（不重复加载）；都没有就在当前线程加载
	std::shared_ptr<AssetObject> LoadAsset(const AssetGUID& guid);
	// 后台加载：立即返回请求，之后 Wait / IsDone 取结果。已加载的返回已完成的请求。
	// 同一个资产同时只会加载一次，多处请求共用一个
	AssetRequest RequestAsset(const AssetGUID& guid, AssetLoadPriority priority = AssetLoadPriority::Normal);
	// 批量提交（依赖并行加载用）：先全部提交，再逐个 Wait
	std::vector<AssetRequest> RequestAssets(const std::vector<AssetGUID>& guids, AssetLoadPriority priority = AssetLoadPriority::Normal);

	bool SaveAsset(const AssetGUID& guid, std::shared_ptr<AssetObject> newAsset = nullptr);

	AssetLoader& GetLoader() { return *_loader; }

	std::vector<std::shared_ptr<ScriptSourceData>> CheckScriptChanges();
	std::vector<std::shared_ptr<UI::ParseResult>> CheckUIDocumentChanges();

	void ClearLoadedAsset();

private:
	// 资产表里有就返回（脚本顺带检查文件变化）
	std::shared_ptr<AssetObject> FindLoaded(const AssetGUID& guid);
	// 真正的加载：在加载器的任务里执行。generation 变了（期间切换过工程）就不进资产表、返回空
	std::shared_ptr<AssetObject> LoadNow(const AssetGUID& guid, uint64_t generation);
	AssetRequest Submit(const AssetGUID& guid, AssetLoadPriority priority, bool runInlineIfNew);

	std::unique_ptr<AssetLoader> _loader;
	// 每次清空资产表（切换工程）加一：旧工程还在途的请求不会把结果塞进新工程的资产表，也不会被新请求复用
	std::atomic<uint64_t> _generation{ 0 };

	SafeUnorderedMap<AssetGUID, std::shared_ptr<AssetObject>> m_GuidToAsset;	// guid <-> path
	std::shared_ptr<IMetadataProvider> _metaProvider;
	std::shared_ptr<IAssetFullPathProvider> _pathProvider;
};