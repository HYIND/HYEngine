#include "Project/AssetObject.h"
#include "Project/AssetStroage.h"
#include "Coroutine.h"

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
	if (!_material)
	{
		_material = std::make_shared<Material>();
		_material->SetAssetGUID(_assetGUID);
		_material->SetProperty(_properties);
		if (auto storage = _storage.lock())
		{
			for (auto& [type, guid] : _dependencyTexture)
			{
				if (auto asset = storage->LoadAsset(guid))
				{
					if (auto texture = asset->AsTexture())
						_material->SetTexture(type, texture);
				}
			}
		}
	}
	return _material;
}

MaterialProperties MaterialAsset::GetProperties() const
{
	return _properties;
}

void MaterialAsset::SetProperties(const MaterialProperties& properties)
{
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
	if (guid.empty())
		_dependencyTexture.erase(type);
	else
		_dependencyTexture[type] = guid;
	if (_material)
		_material->SetTexture(type, texture);
}

StaticModelAsset::StaticModelAsset() { _type = AssetType::StaticModel; }

std::shared_ptr<StaticModel> StaticModelAsset::AsStaticModel()
{
	std::vector<std::shared_ptr<StaticMeshInfo>> mesheInfos;
	if (auto storage = _storage.lock())
	{
		std::vector<Task<std::vector<std::shared_ptr<StaticMesh>>>> staticMeshTask;
		std::vector<Task<void>> materialTask;

		for (auto& guid : _dependencyStaticMesh)
		{
			auto task = CoroTask::Run([guid, &storage]() -> std::vector<std::shared_ptr<StaticMesh>> {
				auto asset = storage->LoadAsset(guid);
				if (asset)
					return asset->AsStaticMesh();
				return {};
				});
			staticMeshTask.push_back(std::move(task));
		}

		for (auto& task : staticMeshTask)
		{
			for (auto& mesh : task.sync_wait())
			{
				auto meshInfo = std::make_shared<StaticMeshInfo>();
				meshInfo->mesh = mesh;
				mesheInfos.push_back(std::move(meshInfo));
			}
		}

		for (uint32_t i = 0; i < mesheInfos.size(); i++)
		{
			auto task = CoroTask::Run([i, &storage, &mesheInfos, this]() ->auto {
				std::shared_ptr<Material> material;
				if (_dependencyMaterial.find(i) != _dependencyMaterial.end())
				{
					auto& guid = _dependencyMaterial[i];
					auto asset = storage->LoadAsset(guid);
					if (asset)
						material = asset->AsMaterial();
				}

				if (!material)
					material = std::make_shared<Material>();

				mesheInfos[i]->material = material;
				});
			materialTask.push_back(std::move(task));
		}

		for (auto& task : materialTask)
			task.sync_wait();
	}

	auto model = std::make_shared<StaticModel>();
	model->SetAssetGUID(_assetGUID);
	for (auto& info : mesheInfos)
		model->AddMesh(info->mesh, info->material);

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
	std::vector<std::shared_ptr<SkinnedMeshInfo>> mesheInfos;
	if (auto storage = _storage.lock())
	{
		std::vector<Task<std::vector<std::shared_ptr<SkinnedMesh>>>> skinnedMeshTask;
		std::vector<Task<void>> materialTask;

		for (auto& guid : _dependencySkinnedMesh)
		{
			auto task = CoroTask::Run([guid, &storage]() -> std::vector<std::shared_ptr<SkinnedMesh>> {
				auto asset = storage->LoadAsset(guid);
				if (asset)
					return asset->AsSkinnedMesh();
				return {};
				});
			skinnedMeshTask.push_back(std::move(task));
		}

		for (auto& task : skinnedMeshTask)
		{
			for (auto& mesh : task.sync_wait())
			{
				auto meshInfo = std::make_shared<SkinnedMeshInfo>();
				meshInfo->mesh = mesh;
				mesheInfos.push_back(std::move(meshInfo));
			}
		}

		for (uint32_t i = 0; i < mesheInfos.size(); i++)
		{
			auto task = CoroTask::Run([i, &storage, &mesheInfos, this]() ->auto {
				std::shared_ptr<Material> material;
				if (_dependencyMaterial.find(i) != _dependencyMaterial.end())
				{
					auto& guid = _dependencyMaterial[i];
					auto asset = storage->LoadAsset(guid);
					if (asset)
						material = asset->AsMaterial();
				}

				if (!material)
					material = std::make_shared<Material>();

				mesheInfos[i]->material = material;
				});
			materialTask.push_back(std::move(task));
		}

		for (auto& task : materialTask)
			task.sync_wait();
	}

	auto model = std::make_shared<SkinnedModel>();
	model->SetAssetGUID(_assetGUID);
	for (auto& info : mesheInfos)
		model->AddMesh(info->mesh, info->material);

	return model;
}

SkeletonAsset::SkeletonAsset() { _type = AssetType::Skeleton; }

SkeletonAsset::~SkeletonAsset() { _skeleton.reset(); }

std::shared_ptr<Skeleton> SkeletonAsset::AsSkeleton() { return _skeleton; }

AnimationClipAsset::AnimationClipAsset() { _type = AssetType::AnimationClip; }

AnimationClipAsset::~AnimationClipAsset() { _animationClip.reset(); }

std::shared_ptr<AnimationClip> AnimationClipAsset::AsAnimationClip() { return _animationClip; }