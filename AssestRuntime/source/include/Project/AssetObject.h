#pragma once

#include "AssetMeta.h"
#include "AssetDescription.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "SceneCodec/SceneCodec.h"
#include "Script/ScriptSourceData.h"
#include "UI/UiDocumentParser.h"
#include <filesystem>

class AssetStroage;
class AssetStroageHelper;

class AudioInfo;

class AssetObject
{
public:
	AssetType GetType() const;
	const AssetGUID& GetAssetGUID() const;

public:
	virtual ~AssetObject() = default;

	virtual std::vector<std::shared_ptr<StaticMesh>> AsStaticMesh();
	virtual std::shared_ptr<Texture2D> AsTexture();
	virtual std::shared_ptr<Material> AsMaterial();
	virtual std::shared_ptr<StaticModel> AsStaticModel();
	virtual std::shared_ptr<SceneCodec> AsScene();
	virtual std::shared_ptr<AudioInfo> AsAudio();
	virtual std::shared_ptr<ScriptSourceData> AsScript();
	virtual std::shared_ptr<UI::ParseResult> AsUIDocument();
	virtual std::shared_ptr<UI::UiStyle> AsUIStyle();

public:
	virtual bool NeedUpdate() { return false; }

protected:
	AssetType _type;
	AssetGUID _assetGUID;
	std::weak_ptr<AssetStroage> _storage;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class StaticMeshAsset : public AssetObject
{
public:
	StaticMeshAsset();
	virtual ~StaticMeshAsset() {
		_meshes.clear();
	}
	virtual std::vector<std::shared_ptr<StaticMesh>> AsStaticMesh() override;

private:
	std::vector<std::shared_ptr<StaticMesh>> _meshes;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class TextureAsset : public AssetObject
{
public:
	TextureAsset();
	virtual ~TextureAsset();
	virtual std::shared_ptr<Texture2D> AsTexture() override;

private:
	std::shared_ptr<Texture2D> _texture;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class AudioAsset : public AssetObject
{
public:
	AudioAsset();
	virtual ~AudioAsset() = default;
	virtual std::shared_ptr<AudioInfo> AsAudio() override;

private:
	std::shared_ptr<AudioInfo> _audio;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class MaterialAsset : public AssetObject
{
public:
	MaterialAsset();
	virtual ~MaterialAsset() = default;
	virtual std::shared_ptr<Material> AsMaterial() override;
	MaterialProperties GetProperties() const;
	void SetProperties(const MaterialProperties& properties);
	const std::map<TextureType, AssetGUID>& GetTextureDependencies() const;
	void SetTextureDependency(TextureType type, const AssetGUID& guid, const std::shared_ptr<Texture2D>& texture);

private:
	MaterialProperties _properties;
	std::map<TextureType, AssetGUID> _dependencyTexture;
	std::shared_ptr<Material> _material;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class ModelAsset : public AssetObject
{
public:
	ModelAsset();
	virtual ~ModelAsset() = default;
	virtual std::shared_ptr<StaticModel> AsStaticModel() override;

private:
	std::vector<AssetGUID> _dependencyStaticMesh;
	std::unordered_map<uint32_t, AssetGUID> _dependencyMaterial;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class SceneAsset : public AssetObject
{
public:
	SceneAsset();
	virtual ~SceneAsset() = default;
	virtual std::shared_ptr<SceneCodec> AsScene() override;

	void SetScene(std::shared_ptr<SceneCodec>);

private:
	std::shared_ptr<SceneCodec> _scene;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class ScriptAsset : public AssetObject
{
public:
	ScriptAsset();
	virtual ~ScriptAsset() = default;
	virtual std::shared_ptr<ScriptSourceData> AsScript() override;

	void SetData(const std::shared_ptr<ScriptSourceData>& data);

private:
	std::shared_ptr<ScriptSourceData> _data;
	std::filesystem::file_time_type _lastWriteTime{};
	friend AssetStroage;
	friend AssetStroageHelper;
};


// UI 文档（.ui）：保存解析结果，节点树、样式引用、每节点有效样式都在里面。
class UIDocumentAsset : public AssetObject
{
public:
	UIDocumentAsset();
	virtual ~UIDocumentAsset() = default;
	virtual std::shared_ptr<UI::ParseResult> AsUIDocument() override;

	const std::shared_ptr<UI::ParseResult>& GetDocument() const { return _document; }

private:
	std::shared_ptr<UI::ParseResult> _document;
	// 记录上次解析时文件的修改时间，用于热重载检测。
	std::filesystem::file_time_type _lastWriteTime{};
	friend AssetStroage;
	friend AssetStroageHelper;
};

// UI 样式（.uistyle）：单份样式资产，样式引用在文档解析时内联展开。
class UIStyleAsset : public AssetObject
{
public:
	UIStyleAsset();
	virtual ~UIStyleAsset() = default;
	virtual std::shared_ptr<UI::UiStyle> AsUIStyle() override;

	const std::shared_ptr<UI::UiStyle>& GetStyle() const { return _style; }

private:
	std::shared_ptr<UI::UiStyle> _style;
	std::filesystem::file_time_type _lastWriteTime{};
	friend AssetStroage;
	friend AssetStroageHelper;
};
