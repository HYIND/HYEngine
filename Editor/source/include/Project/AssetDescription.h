#pragma once

#include "AssetMeta.h"
#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/Base/Material.h"
#include <string>

class AssetDescription
{
public:
	virtual ~AssetDescription() = default;
	virtual bool LoadFromFile(const std::string& filePath) = 0;
	virtual bool SaveToFile(const std::string& filePath) = 0;
};

class StaticMeshAssetDescription : public AssetDescription
{
public:
	virtual bool LoadFromFile(const std::string& filePath) override;
	virtual bool SaveToFile(const std::string& filePath) override;

	std::vector<AssetPath>& GetPath();

	void SetPath(const std::vector<AssetPath>& paths);

private:
	std::vector<AssetPath> _paths;
};

class TextureAssetDescription : public AssetDescription
{
public:
	virtual bool LoadFromFile(const std::string& filePath) override;
	virtual bool SaveToFile(const std::string& filePath) override;

	AssetPath GetPath() const;
	TextureConfig GetConfig() const;

	void SetPath(const AssetPath& assetPath);
	void SetConfig(const TextureConfig& config);

private:
	AssetPath _path;
	TextureConfig _config;
};

class AduioAssetDescription : public AssetDescription
{
public:
	virtual bool LoadFromFile(const std::string& filePath) override;
	virtual bool SaveToFile(const std::string& filePath) override;

	AssetPath GetPath() const;

	void SetPath(const AssetPath& assetPath);

private:
	AssetPath _path;
};

class MaterialAssetDescription : public AssetDescription
{
public:
	virtual bool LoadFromFile(const std::string& filePath) override;
	virtual bool SaveToFile(const std::string& filePath) override;

	MaterialProperties GetProperties() const;
	std::map<TextureType, AssetGUID> GetTexture() const;

	void SetProperties(const MaterialProperties& prop);
	void SetTexture(TextureType type, const AssetGUID& guid);
	void SetTexture(const std::map<TextureType, AssetGUID>& map);

private:
	MaterialProperties _prop;
	std::map<TextureType, AssetGUID> _dependencyTexture;
};

class ModelAssetDescription : public AssetDescription
{
public:
	virtual bool LoadFromFile(const std::string& filePath) override;
	virtual bool SaveToFile(const std::string& filePath) override;

	std::vector<AssetGUID> GetStaticMesh() const;;
	std::unordered_map<uint32_t, AssetGUID> GetMaterial() const;

	void AddStaticMesh(const AssetGUID& guid);
	void SetMaterial(uint32_t index, const AssetGUID& guid);
	void SetMaterial(const std::unordered_map<uint32_t, AssetGUID>& materials);

private:
	std::vector<AssetGUID> _dependencyStaticMesh;
	std::unordered_map<uint32_t, AssetGUID> _dependencyMaterial;
};
