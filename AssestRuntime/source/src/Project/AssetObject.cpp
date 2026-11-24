#include "Project/AssetObject.h"
#include "Project/AssetStroage.h"

AssetType AssetObject::GetType() const {
	return _type;
}

const AssetGUID& AssetObject::GetAssetGUID() const {
	return _assetGUID;
}

std::vector<std::shared_ptr<StaticMesh>> AssetObject::AsStaticMesh() { return {}; }

std::shared_ptr<Texture2D> AssetObject::AsTexture() { return nullptr; }

std::shared_ptr<Material> AssetObject::AsMaterial() { return nullptr; }

std::shared_ptr<StaticModel> AssetObject::AsStaticModel() { return nullptr; }

std::shared_ptr<SceneCodec> AssetObject::AsScene() { return nullptr; }

std::shared_ptr<AudioInfo> AssetObject::AsAudio() { return nullptr; }

std::shared_ptr<ScriptSourceData> AssetObject::AsScript() { return nullptr; }

std::shared_ptr<UI::ParseResult> AssetObject::AsUIDocument() { return nullptr; }

std::shared_ptr<UI::UiStyle> AssetObject::AsUIStyle() { return nullptr; }

std::vector<std::shared_ptr<SkinnedMesh>> AssetObject::AsSkinnedMesh() { return {}; }

std::shared_ptr<SkinnedModel> AssetObject::AsSkinnedModel() { return nullptr; }

std::shared_ptr<Skeleton> AssetObject::AsSkeleton() { return nullptr; }

std::shared_ptr<AnimationClip> AssetObject::AsAnimationClip() { return nullptr; }

std::shared_ptr<Render2D::Sprite> AssetObject::AsSprite() { return nullptr; }

std::shared_ptr<Render2D::AnimatedImage> AssetObject::AsAnimatedImage() { return nullptr; }


StaticMeshAsset::StaticMeshAsset() { _type = AssetType::StaticMesh; }

std::vector<std::shared_ptr<StaticMesh>> StaticMeshAsset::AsStaticMesh() { return _meshes; }

TextureAsset::TextureAsset() { _type = AssetType::Texture; }

TextureAsset::~TextureAsset() { _texture.reset(); }

std::shared_ptr<Texture2D> TextureAsset::AsTexture() { return _texture; }

std::shared_ptr<Render2D::Sprite> TextureAsset::AsSprite()
{
	std::lock_guard<std::mutex> lock(_spriteMutex);
	if (!_sprite && _texture && !_texture->IsEmpty())
	{
		_sprite = std::make_shared<Render2D::Sprite>(_texture, std::vector<Render2D::SpriteRegion>{});
		_sprite->SetAssetGUID(_assetGUID);
	}
	return _sprite;
}

// ============================== Sprite ==============================

SpriteAsset::SpriteAsset() { _type = AssetType::Sprite; }

std::shared_ptr<Render2D::Sprite> SpriteAsset::AsSprite()
{
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_sprite)
	{
		// 正常在加载时就建好了；这里只是兜底，保证引用方拿到的对象不为空
		_sprite = std::make_shared<Render2D::Sprite>(nullptr, _regions);
		_sprite->SetAssetGUID(_assetGUID);
	}
	return _sprite;
}

std::shared_ptr<Texture2D> SpriteAsset::AsTexture()
{
	auto sprite = AsSprite();
	return sprite ? sprite->GetTexture() : nullptr;
}

AssetPath SpriteAsset::GetPath() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _path;
}

TextureConfig SpriteAsset::GetConfig() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _config;
}

std::vector<Render2D::SpriteRegion> SpriteAsset::GetRegions() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _regions;
}

bool SpriteAsset::SetConfig(const TextureConfig& config, const std::string& sourceFullPath)
{
	std::string error;
	auto texture = Render2D::LoadImageTexture(sourceFullPath, config, &error);
	if (!texture)
	{
		std::cerr << std::format("[ Asset ] Sprite 重新加载图片失败：{}\n", error);
		return false;
	}
	texture->SetAssetGUID(_assetGUID);

	auto sprite = AsSprite();
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_config = config;
	}
	sprite->SetTexture(std::move(texture));
	return true;
}

void SpriteAsset::SetRegions(const std::vector<Render2D::SpriteRegion>& regions)
{
	std::shared_ptr<Render2D::Sprite> sprite;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_regions = regions;
		sprite = _sprite;
	}
	if (sprite)
		sprite->SetRegions(regions);
}

// ============================== AnimatedImage ==============================

AnimatedImageAsset::AnimatedImageAsset() { _type = AssetType::AnimatedImage; }

std::shared_ptr<Render2D::AnimatedImage> AnimatedImageAsset::AsAnimatedImage()
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _image;
}

bool AnimatedImageAsset::SetConfig(const TextureConfig& config, const std::string& sourceFullPath)
{
	std::string error;
	auto image = Render2D::AnimatedImage::LoadFromFile(sourceFullPath, config, &error);
	if (!image)
	{
		std::cerr << std::format("[ Asset ] 动图重新解码失败：{}\n", error);
		return false;
	}
	image->SetAssetGUID(_assetGUID);

	std::lock_guard<std::mutex> lock(_mutex);
	_description.SetConfig(config);
	if (_image)
		_image->MarkStale();
	_image = std::move(image);
	return true;
}

AudioAsset::AudioAsset() { _type = AssetType::Audio; }

std::shared_ptr<AudioInfo> AudioAsset::AsAudio() { return _audio; }

MaterialAsset::MaterialAsset() { _type = AssetType::Material; }

std::shared_ptr<Material> MaterialAsset::AsMaterial()
{
	{
		std::lock_guard lock(_mutex);
		if (_material)
			return _material;
	}

	std::lock_guard build(_buildMutex);
	std::map<TextureType, AssetGUID> textures;
	MaterialProperties properties;
	{
		std::lock_guard lock(_mutex);
		if (_material)
			return _material;	// 等锁期间别的线程已经建好了
		textures = _dependencyTexture;
		properties = _properties;
	}

	auto material = std::make_shared<Material>();
	material->SetAssetGUID(_assetGUID);
	material->SetProperty(properties);
	if (auto storage = _storage.lock())
	{
		// 先全部提交再逐个等：还在排队的由当前线程直接执行，其余的线程池并行做
		std::vector<std::pair<TextureType, AssetStroage::AssetRequest>> requests;
		requests.reserve(textures.size());
		for (const auto& [type, guid] : textures)
			requests.emplace_back(type, storage->RequestAsset(guid));
		for (auto& [type, request] : requests)
		{
			auto asset = request ? request->WaitResult() : nullptr;
			if (auto texture = asset ? asset->AsTexture() : nullptr)
				material->SetTexture(type, texture);
		}
	}

	std::lock_guard lock(_mutex);
	if (!_material)
		_material = std::move(material);
	return _material;
}

void MaterialAsset::PrefetchTextures()
{
	std::vector<AssetGUID> guids;
	{
		std::lock_guard lock(_mutex);
		if (_material)
			return;
		for (const auto& [type, guid] : _dependencyTexture)
			guids.push_back(guid);
	}
	if (auto storage = _storage.lock())
		storage->RequestAssets(guids);
}

MaterialProperties MaterialAsset::GetProperties() const
{
	std::lock_guard lock(_mutex);
	return _properties;
}

void MaterialAsset::SetProperties(const MaterialProperties& properties)
{
	std::lock_guard lock(_mutex);
	_properties = properties;
	if (_material)
		_material->SetProperty(properties);
}

const std::map<TextureType, AssetGUID>& MaterialAsset::GetTextureDependencies() const
{
	return _dependencyTexture;
}

void MaterialAsset::SetTextureDependency(TextureType type, const AssetGUID& guid, const std::shared_ptr<Texture2D>& texture)
{
	std::lock_guard lock(_mutex);
	if (guid.empty())
		_dependencyTexture.erase(type);
	else
		_dependencyTexture[type] = guid;
	if (_material)
		_material->SetTexture(type, texture);
}

namespace
{
	// 模型的依赖加载：网格和材质一次性全部提交给加载器（同一个资产全局只加载一次），
	// 材质资产一到手就把它们的贴图也提交出去，最后按网格顺序组装。
	// 等待时还在排队的由当前线程直接执行，所以不会出现线程池里的任务互相干等
	template<typename MeshPtr, typename AsMeshes>
	void ResolveModelDependencies(AssetStroage& storage, const std::vector<AssetGUID>& meshGuids, const std::unordered_map<uint32_t, AssetGUID>& materialGuids,
		AsMeshes asMeshes, std::vector<MeshPtr>& outMeshes, std::vector<std::shared_ptr<Material>>& outMaterials)
	{
		auto meshRequests = storage.RequestAssets(meshGuids);
		std::unordered_map<uint32_t, AssetStroage::AssetRequest> materialRequests;
		for (const auto& [index, guid] : materialGuids)
			materialRequests.emplace(index, storage.RequestAsset(guid));

		// 材质资产本身很小，先等它们，好尽早把所有贴图一起提交（各材质的贴图并行，而不是一个材质一个材质地来）
		std::unordered_map<uint32_t, std::shared_ptr<MaterialAsset>> materialAssets;
		for (auto& [index, request] : materialRequests)
		{
			auto asset = std::dynamic_pointer_cast<MaterialAsset>(request ? request->WaitResult() : nullptr);
			if (asset)
			{
				asset->PrefetchTextures();
				materialAssets.emplace(index, std::move(asset));
			}
		}

		for (auto& request : meshRequests)
		{
			auto asset = request ? request->WaitResult() : nullptr;
			if (!asset)
				continue;
			for (auto& mesh : asMeshes(*asset))
				outMeshes.push_back(std::move(mesh));
		}

		outMaterials.resize(outMeshes.size());
		for (uint32_t i = 0; i < outMeshes.size(); ++i)
		{
			auto it = materialAssets.find(i);
			std::shared_ptr<Material> material = it != materialAssets.end() ? it->second->AsMaterial() : nullptr;
			outMaterials[i] = material ? std::move(material) : std::make_shared<Material>();
		}
	}
}

StaticModelAsset::StaticModelAsset() { _type = AssetType::StaticModel; }

std::shared_ptr<StaticModel> StaticModelAsset::AsStaticModel()
{
	std::vector<std::shared_ptr<StaticMesh>> meshes;
	std::vector<std::shared_ptr<Material>> materials;
	if (auto storage = _storage.lock())
	{
		ResolveModelDependencies(*storage, _dependencyStaticMesh, _dependencyMaterial,
			[](AssetObject& asset) { return asset.AsStaticMesh(); }, meshes, materials);
	}

	auto model = std::make_shared<StaticModel>();
	model->SetAssetGUID(_assetGUID);
	for (size_t i = 0; i < meshes.size(); ++i)
		model->AddMesh(meshes[i], materials[i]);

	return model;
}

SceneAsset::SceneAsset() { _type = AssetType::Scene; }

std::shared_ptr<SceneCodec> SceneAsset::AsScene()
{
	return _scene;
}

void SceneAsset::SetScene(std::shared_ptr<SceneCodec> s)
{
	_scene = s;
}

ScriptAsset::ScriptAsset() { _type = AssetType::Script; }

std::shared_ptr<ScriptSourceData> ScriptAsset::AsScript()
{
	return _data;
}

void ScriptAsset::SetData(const std::shared_ptr<ScriptSourceData>& data)
{
	_data = data;
}

UIDocumentAsset::UIDocumentAsset() { _type = AssetType::UiDocument; }

std::shared_ptr<UI::ParseResult> UIDocumentAsset::AsUIDocument() { return _document; }

UIStyleAsset::UIStyleAsset() { _type = AssetType::UiStyle; }

std::shared_ptr<UI::UiStyle> UIStyleAsset::AsUIStyle() { return _style; }

SkinnedMeshAsset::SkinnedMeshAsset() { _type = AssetType::SkinnedMesh; }

std::vector<std::shared_ptr<SkinnedMesh>> SkinnedMeshAsset::AsSkinnedMesh() { return _meshes; }

SkinnedModelAsset::SkinnedModelAsset() { _type = AssetType::SkinnedModel; }

std::shared_ptr<SkinnedModel> SkinnedModelAsset::AsSkinnedModel()
{
	std::vector<std::shared_ptr<SkinnedMesh>> meshes;
	std::vector<std::shared_ptr<Material>> materials;
	if (auto storage = _storage.lock())
	{
		ResolveModelDependencies(*storage, _dependencySkinnedMesh, _dependencyMaterial,
			[](AssetObject& asset) { return asset.AsSkinnedMesh(); }, meshes, materials);
	}

	auto model = std::make_shared<SkinnedModel>();
	model->SetAssetGUID(_assetGUID);
	for (size_t i = 0; i < meshes.size(); ++i)
		model->AddMesh(meshes[i], materials[i]);

	return model;
}

SkeletonAsset::SkeletonAsset() { _type = AssetType::Skeleton; }

SkeletonAsset::~SkeletonAsset() { _skeleton.reset(); }

std::shared_ptr<Skeleton> SkeletonAsset::AsSkeleton() { return _skeleton; }

AnimationClipAsset::AnimationClipAsset() { _type = AssetType::AnimationClip; }

AnimationClipAsset::~AnimationClipAsset() { _animationClip.reset(); }

std::shared_ptr<AnimationClip> AnimationClipAsset::AsAnimationClip() { return _animationClip; }