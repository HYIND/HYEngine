#include "Project/AssetObject.h"
#include "Project/AssetStroage.h"

AssetType AssetObject::GetType() const {
	return _type;
}

const AssetGUID& AssetObject::GetAssetGUID() const {
	return _assetGUID;
}

std::vector<std::shared_ptr<Mesh>> AssetObject::AsStaticMesh() { return {}; }

std::shared_ptr<Texture2D> AssetObject::AsTexture() { return nullptr; }

std::shared_ptr<Material> AssetObject::AsMaterial() { return nullptr; }

std::shared_ptr<Model> AssetObject::AsModel() { return nullptr; }

std::shared_ptr<SceneCodec> AssetObject::AsScene() { return nullptr; }

std::shared_ptr<AudioInfo> AssetObject::AsAudio() { return nullptr; }

std::shared_ptr<ScriptSourceData> AssetObject::AsScript() { return nullptr; }

StaticMeshAsset::StaticMeshAsset() { _type = AssetType::StaticMesh; }

std::vector<std::shared_ptr<Mesh>> StaticMeshAsset::AsStaticMesh() { return _meshes; }

TextureAsset::TextureAsset() { _type = AssetType::Texture; }

TextureAsset::~TextureAsset() { _texture.reset(); }

std::shared_ptr<Texture2D> TextureAsset::AsTexture() { return _texture; }

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

ModelAsset::ModelAsset() { _type = AssetType::Model; }

std::shared_ptr<Model> ModelAsset::AsModel()
{
	std::vector<std::shared_ptr<MeshInfo>> mesheInfos;
	if (auto storage = _storage.lock())
	{
		std::vector<Task<std::vector<std::shared_ptr<Mesh>>>> staticMeshTask;
		std::vector<Task<void>> materialTask;

		for (auto& guid : _dependencyStaticMesh)
		{
			auto task = CoroTask::Run([guid, &storage]() -> std::vector<std::shared_ptr<Mesh>> {
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
				auto meshInfo = std::make_shared<MeshInfo>();
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

	auto model = std::make_shared<Model>();
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
