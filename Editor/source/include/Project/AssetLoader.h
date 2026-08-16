#pragma once

#include "AssetMeta.h"
#include "AssetObject.h"

class IAssetFullPathProvider
{
public:
	virtual ~IAssetFullPathProvider() = default;
	virtual std::string GetFullPath(const AssetPath& assetPath) = 0;
};

class AssetLoader :public std::enable_shared_from_this<AssetLoader>
{
public:
	void SetMetadataProvider(std::shared_ptr<IMetadataProvider> provider);
	void SetPathProvider(std::shared_ptr<IAssetFullPathProvider> provider);

	std::shared_ptr<AssetObject> LoadAsset(const AssetGUID& guid);
	void ClearLoadedAsset();

private:
	SafeUnorderedMap<AssetGUID, std::shared_ptr<AssetObject>> m_GuidToAsset;	// guid <-> path
	std::shared_ptr<IMetadataProvider> _metaProvider;
	std::shared_ptr<IAssetFullPathProvider> _pathProvider;
};