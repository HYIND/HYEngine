#pragma once

#include "AssetMeta.h"
#include "AssetDescription.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "GeneralManager/AudioDeviceManager.h"

class AssetLoader;
class AssetLoadHelper;

class AssetObject
{
public:
	AssetType GetType() const;

public:
	virtual ~AssetObject() = default;

	virtual std::vector<std::shared_ptr<Mesh>> AsStaticMesh();
	virtual std::shared_ptr<Texture2D> AsTexture();
	virtual std::shared_ptr<Material> AsMaterial();
	virtual std::shared_ptr<Model> AsModel();
	virtual std::shared_ptr<Scene> AsScene();
	virtual std::shared_ptr<AudioInfo> AsAudio();

protected:
	AssetType _type;
	std::weak_ptr<AssetLoader> _loader;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class StaticMeshAsset : public AssetObject
{
public:
	StaticMeshAsset();
	virtual std::vector<std::shared_ptr<Mesh>> AsStaticMesh() override;

private:
	std::vector<std::shared_ptr<Mesh>> _meshes;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class TextureAsset : public AssetObject
{
public:
	TextureAsset();
	virtual std::shared_ptr<Texture2D> AsTexture() override;

private:
	std::shared_ptr<Texture2D> _texture;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class AudioAsset : public AssetObject
{
public:
	AudioAsset();
	virtual std::shared_ptr<AudioInfo> AsAudio() override;

private:
	std::shared_ptr<AudioInfo> _audio;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class MaterialAsset : public AssetObject
{
public:
	MaterialAsset();
	virtual std::shared_ptr<Material> AsMaterial() override;

private:
	MaterialProperties _properties;
	std::map<TextureType, AssetGUID> _dependencyTexture;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class ModelAsset : public AssetObject
{
public:
	ModelAsset();
	virtual std::shared_ptr<Model> AsModel() override;

private:
	std::vector<AssetGUID> _dependencyStaticMesh;
	std::unordered_map<uint32_t, AssetGUID> _dependencyMaterial;
	friend AssetLoader;
	friend AssetLoadHelper;
};

class SceneAsset : public AssetObject
{
public:
	SceneAsset();
	virtual std::shared_ptr<Scene> AsScene() override;

private:
	friend AssetLoader;
	friend AssetLoadHelper;
};
