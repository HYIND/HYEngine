#pragma once

#include "AssetMeta.h"
#include "AssetObject.h"
#include "SafeStl.h"

class IAssetFullPathProvider
{
public:
	virtual ~IAssetFullPathProvider() = default;
	virtual std::string GetFullPath(const AssetPath& assetPath) = 0;
};

class AssetStroage :public std::enable_shared_from_this<AssetStroage>
{
public:
	void SetMetadataProvider(std::shared_ptr<IMetadataProvider> provider);
	void SetPathProvider(std::shared_ptr<IAssetFullPathProvider> provider);

	std::shared_ptr<AssetObject> LoadAsset(const AssetGUID& guid);
	bool SaveAsset(const AssetGUID& guid, std::shared_ptr<AssetObject> newAsset = nullptr);

	void ClearLoadedAsset();

private:
	SafeUnorderedMap<AssetGUID, std::shared_ptr<AssetObject>> m_GuidToAsset;	// guid <-> path
	std::shared_ptr<IMetadataProvider> _metaProvider;
	std::shared_ptr<IAssetFullPathProvider> _pathProvider;
};