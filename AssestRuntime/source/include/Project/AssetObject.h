#pragma once

#include "AssetMeta.h"
#include "AssetDescription.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"
#include "SceneCodec/SceneCodec.h"
#include "Script/ScriptSourceData.h"
#include "UI/UiDocumentParser.h"
#include <filesystem>
#include <mutex>

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
	virtual std::vector<std::shared_ptr<SkinnedMesh>> AsSkinnedMesh();
	virtual std::shared_ptr<SkinnedModel> AsSkinnedModel();
	virtual std::shared_ptr<Skeleton> AsSkeleton();
	virtual std::shared_ptr<AnimationClip> AsAnimationClip();
	// 2D 图元：Sprite 资产返回自身；Texture 资产返回一个"整张贴图、无区域"的 Sprite，方便直接拿贴图当 Sprite 用
	virtual std::shared_ptr<Render2D::Sprite> AsSprite();
	virtual std::shared_ptr<Render2D::AnimatedImage> AsAnimatedImage();

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
	virtual std::shared_ptr<Render2D::Sprite> AsSprite() override;

private:
	std::shared_ptr<Texture2D> _texture;
	std::shared_ptr<Render2D::Sprite> _sprite;
	std::mutex _spriteMutex;
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

// Sprite：自己从原始图片加载贴图（不经过 Texture 资产）
class SpriteAsset : public AssetObject
{
public:
	SpriteAsset();
	virtual ~SpriteAsset() = default;
	virtual std::shared_ptr<Render2D::Sprite> AsSprite() override;
	// 方便 UI / 编辑器预览：Sprite 自己的那张贴图
	virtual std::shared_ptr<Texture2D> AsTexture() override;

	AssetPath GetPath() const;
	TextureConfig GetConfig() const;
	std::vector<Render2D::SpriteRegion> GetRegions() const;
	// 编辑器修改后立即作用到运行时对象（已引用它的实体马上看到效果），存盘走 SaveAsset
	void SetRegions(const std::vector<Render2D::SpriteRegion>& regions);
	// 改导入配置（采样过滤 / sRGB）：按新配置重新加载图片，运行时对象直接换贴图
	bool SetConfig(const TextureConfig& config, const std::string& sourceFullPath);

private:
	mutable std::mutex _mutex;
	AssetPath _path;
	TextureConfig _config;
	std::vector<Render2D::SpriteRegion> _regions;
	std::shared_ptr<Render2D::Sprite> _sprite;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class AnimatedImageAsset : public AssetObject
{
public:
	AnimatedImageAsset();
	virtual ~AnimatedImageAsset() = default;
	virtual std::shared_ptr<Render2D::AnimatedImage> AsAnimatedImage() override;

	const AnimatedImageAssetDescription& GetDescription() const { return _description; }
	// 改导入配置（采样过滤 / sRGB）：存盘后重新解码生成图集；已引用旧图集的地方下一次解析时换成新的
	bool SetConfig(const TextureConfig& config, const std::string& sourceFullPath);

private:
	AnimatedImageAssetDescription _description;
	std::shared_ptr<Render2D::AnimatedImage> _image;
	mutable std::mutex _mutex;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class StaticModelAsset : public AssetObject
{
public:
	StaticModelAsset();
	virtual ~StaticModelAsset() = default;
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

class UIDocumentAsset : public AssetObject
{
public:
	UIDocumentAsset();
	virtual ~UIDocumentAsset() = default;
	virtual std::shared_ptr<UI::ParseResult> AsUIDocument() override;

	const std::shared_ptr<UI::ParseResult>& GetDocument() const { return _document; }

private:
	std::shared_ptr<UI::ParseResult> _document;
	std::filesystem::file_time_type _lastWriteTime{};
	friend AssetStroage;
	friend AssetStroageHelper;
};

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

class SkinnedMeshAsset : public AssetObject
{
public:
	SkinnedMeshAsset();
	virtual ~SkinnedMeshAsset() {
		_meshes.clear();
	}
	virtual std::vector<std::shared_ptr<SkinnedMesh>> AsSkinnedMesh() override;

private:
	std::vector<std::shared_ptr<SkinnedMesh>> _meshes;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class SkinnedModelAsset : public AssetObject
{
public:
	SkinnedModelAsset();
	virtual ~SkinnedModelAsset() = default;
	virtual std::shared_ptr<SkinnedModel> AsSkinnedModel() override;

private:
	std::vector<AssetGUID> _dependencySkinnedMesh;
	std::unordered_map<uint32_t, AssetGUID> _dependencyMaterial;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class SkeletonAsset : public AssetObject
{
public:
	SkeletonAsset();
	virtual ~SkeletonAsset();
	virtual std::shared_ptr<Skeleton> AsSkeleton() override;

private:
	std::shared_ptr<Skeleton> _skeleton;
	friend AssetStroage;
	friend AssetStroageHelper;
};

class AnimationClipAsset : public AssetObject
{
public:
	AnimationClipAsset();
	virtual ~AnimationClipAsset();
	virtual std::shared_ptr<AnimationClip> AsAnimationClip() override;

private:
	std::shared_ptr<AnimationClip> _animationClip;
	friend AssetStroage;
	friend AssetStroageHelper;
};