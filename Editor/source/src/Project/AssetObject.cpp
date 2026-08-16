#include "Project/AssetObject.h"
#include "Project/AssetLoader.h"

AssetType AssetObject::GetType() const {
	return _type;
}

std::vector<std::shared_ptr<Mesh>> AssetObject::AsStaticMesh() { return {}; }

std::shared_ptr<Texture2D> AssetObject::AsTexture() { return nullptr; }

std::shared_ptr<Material> AssetObject::AsMaterial() { return nullptr; }

std::shared_ptr<Model> AssetObject::AsModel() { return nullptr; }

std::shared_ptr<Scene> AssetObject::AsScene() { return nullptr; }

std::shared_ptr<AudioInfo> AssetObject::AsAudio() { return nullptr; }

StaticMeshAsset::StaticMeshAsset() { _type = AssetType::StaticMesh; }

std::vector<std::shared_ptr<Mesh>> StaticMeshAsset::AsStaticMesh() { return _meshes; }

TextureAsset::TextureAsset() { _type = AssetType::Texture; }

std::shared_ptr<Texture2D> TextureAsset::AsTexture() { return _texture; }

AudioAsset::AudioAsset() { _type = AssetType::Audio; }

std::shared_ptr<AudioInfo> AudioAsset::AsAudio() { return _audio; }

MaterialAsset::MaterialAsset() { _type = AssetType::Material; }

std::shared_ptr<Material> MaterialAsset::AsMaterial()
{
	auto material = std::make_shared<Material>();
	material->SetProperty(_properties);
	if (auto loader = _loader.lock())
	{
		for (auto& [type, guid] : _dependencyTexture)
		{
			if (auto texture = loader->LoadAsset(guid)->AsTexture())
				material->SetTexture(type, texture);
		}
	}
	return material;
}

ModelAsset::ModelAsset() { _type = AssetType::Model; }

std::shared_ptr<Model> ModelAsset::AsModel()
{
	std::vector<std::shared_ptr<MeshInfo>> mesheInfos;
	if (auto loader = _loader.lock())
	{
		std::vector<Task<std::vector<std::shared_ptr<Mesh>>>> staticMeshTask;
		std::vector<Task<void>> materialTask;

		for (auto& guid : _dependencyStaticMesh)
		{
			auto task = CoroTask::Run([guid, &loader]() ->auto {
				auto meshes = loader->LoadAsset(guid)->AsStaticMesh();
				return meshes;
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
			auto task = CoroTask::Run([i, &loader, &mesheInfos, this]() ->auto {
				std::shared_ptr<Material> material;
				if (_dependencyMaterial.find(i) != _dependencyMaterial.end())
				{
					auto& guid = _dependencyMaterial[i];
					material = loader->LoadAsset(guid)->AsMaterial();
				}

				if (!material)
					material = std::make_shared<Material>();

				mesheInfos[i]->material = material;
				});
			materialTask.push_back(std::move(task));
		}

		for (auto& task : materialTask)
			task.sync_wait();

		//for (auto& guid : _dependencyStaticMesh)
		//{
		//	auto meshes = loader->LoadAsset(guid)->AsStaticMesh();
		//	for (auto& mesh : meshes)
		//	{
		//		auto meshInfo = std::make_shared<MeshInfo>();
		//		meshInfo->mesh = mesh;
		//		mesheInfos.push_back(std::move(meshInfo));
		//	}
		//}

		//for (uint32_t i = 0; i < mesheInfos.size(); i++)
		//{
		//	std::shared_ptr<Material> material;
		//	if (_dependencyMaterial.find(i) != _dependencyMaterial.end())
		//	{
		//		auto& guid = _dependencyMaterial[i];
		//		material = loader->LoadAsset(guid)->AsMaterial();
		//	}

		//	if (!material)
		//		material = std::make_shared<Material>();

		//	mesheInfos[i]->material = material;
		//}
	}

	auto model = std::make_shared<Model>();
	for (auto& info : mesheInfos)
		model->AddMesh(info->mesh, info->material);

	return model;
}

SceneAsset::SceneAsset() { _type = AssetType::Scene; }

std::shared_ptr<Scene> SceneAsset::AsScene()
{
	return std::shared_ptr<Scene>();
}
