#include "SceneCodec/SceneCodec.h"
#include "Components/Script.h"
#include "Render/Components/RenderStaticModel.h"
#include "Render/Components/RenderSkinnedModel.h"
#include "Render/Components/SkeletonAnimatorGroup.h"
#include "Render/Components/ParticleEmitter.h"
#include "VulkanRenderEngine/Base/Skeleton.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"

using namespace NComponentCodec;

using Json = nlohmann::json;

bool SceneCodec::LoadFromJson(const Json& content)
{
	if (!content.is_object())
		return false;

	std::string name;
	Json settings;
	std::vector<EntityData> loadedEntities;

	try
	{
		const auto formatVersion = content.value("formatVersion", CurrentFormatVersion);
		if (formatVersion > CurrentFormatVersion)
			return false;

		settings = content.value("settings", Json::object());
		Json entities = content.value("entities", Json::array());
		if (!settings.is_object() || !entities.is_array())
			return false;

		name = content.value("name", std::string{});
		loadedEntities.reserve(entities.size());

		for (const auto& entityJson : entities)
		{
			if (!entityJson.is_object())
				return false;

			EntityData entity;
			entity.id = entityJson.value("id", std::string{});
			entity.name = entityJson.value("name", std::string{});
			entity.parentId = entityJson.value("parentId", std::string{});

			Json components = entityJson.value("components", Json::array());
			if (!components.is_array())
				return false;

			entity.components.reserve(components.size());
			for (const auto& componentJson : components)
			{
				if (!componentJson.is_object())
					return false;

				ComponentData component;
				component.type = componentJson.value("type", std::string{});
				if (component.type.empty())
					return false;
				component.data = componentJson.value("data", Json::object());
				entity.components.push_back(std::move(component));
			}

			loadedEntities.push_back(std::move(entity));
		}
	}
	catch (const std::exception&)
	{
		return false;
	}

	_name = std::move(name);
	_settings = std::move(settings);
	_entities = std::move(loadedEntities);
	return true;
}

Json SceneCodec::SaveToJson() const
{
	Json content;
	content["formatVersion"] = CurrentFormatVersion;
	content["name"] = _name;
	content["settings"] = _settings;
	content["entities"] = Json::array();

	for (const auto& entity : _entities)
	{
		Json entityJson;
		entityJson["id"] = entity.id;
		entityJson["name"] = entity.name;
		entityJson["parentId"] = entity.parentId;
		entityJson["components"] = Json::array();
		for (const auto& component : entity.components)
		{
			entityJson["components"].push_back({
				{"type", component.type},
				{"data", component.data}
				});
		}
		content["entities"].push_back(std::move(entityJson));
	}

	return content;
}

bool SceneCodec::Serialize(World& world, const std::function<bool(Entity)>& includeEntity)
{
	std::vector<EntityData> entities;
	const auto& codecs = GetComponentCodecs();

	try
	{
		for (Entity entity : world.getAllEntity())
		{
			if (includeEntity && !includeEntity(entity))
				continue;

			EntityData entityData;
			entityData.id = std::to_string(entity.getId());
			for (const auto& codec : codecs)
			{
				if (!codec.has(entity))
					continue;

				entityData.components.push_back({ codec.type, codec.encode(entity) });
				if (entityData.name.empty() && codec.getName)
					entityData.name = codec.getName(entity);
			}
			entities.push_back(std::move(entityData));
		}
	}
	catch (const std::exception&)
	{
		return false;
	}

	_entities = std::move(entities);
	return true;
}

bool SceneCodec::Deserialize(
	World& world,
	const std::function<std::shared_ptr<ScriptSourceData>(const AssetGUID&)>& resolveScriptAsset,
	const std::function<std::shared_ptr<StaticModel>(const AssetGUID&)>& resolveModelAsset,
	const std::function<std::shared_ptr<SkinnedModel>(const AssetGUID&)>& resolveSkinnedModelAsset,
	const std::function<std::shared_ptr<Skeleton>(const AssetGUID&)>& resolveSkeletonAsset,
	const std::function<std::shared_ptr<AnimationClip>(const AssetGUID&)>& resolveAnimationClipAsset,
	const std::function<std::shared_ptr<Material>(const AssetGUID&)>& resolveMaterialAsset) const
{
	const auto& codecs = GetComponentCodecs();
	std::vector<Entity> entityHandles;
	std::unordered_map<std::string, Entity> entitiesById;

	try
	{
		for (const auto& entityData : _entities)
		{
			Entity entity = world.createEntity();
			entityHandles.push_back(entity);
			if (!entityData.id.empty())
				entitiesById.emplace(entityData.id, entity);
		}

		for (size_t entityIndex = 0; entityIndex < _entities.size(); ++entityIndex)
		{
			const auto& entityData = _entities[entityIndex];
			Entity entity = entityHandles[entityIndex];

			for (const auto& componentData : entityData.components)
			{
				auto codec = std::find_if(codecs.begin(), codecs.end(), [&](const ComponentCodec& item) {
					return item.type == componentData.type;
					});
				if (codec == codecs.end())
					continue;
				if (!codec->decode(entity, componentData.data))
					continue;
			}

			for (const auto& componentData : entityData.components)
			{
				auto codec = std::find_if(codecs.begin(), codecs.end(), [&](const ComponentCodec& item) {
					return item.type == componentData.type;
					});
				if (codec != codecs.end() && codec->resolveReferences && codec->has(entity))
					codec->resolveReferences(entity, componentData.data, entitiesById);
			}

			if (!entityData.name.empty())
			{
				for (const auto& codec : codecs)
				{
					if (codec.setName && codec.has(entity))
					{
						codec.setName(entity, entityData.name);
						break;
					}
				}
			}

		}

		for (Entity entity : entityHandles)
		{
			auto script = entity.tryGetComponent<Script>();
			if (!script || !script->data || script->data->scriptGuid.empty())
				continue;

			if (!resolveScriptAsset)
				continue;

			auto sourceData = resolveScriptAsset(script->data->scriptGuid);
			if (!sourceData || sourceData->scriptGuid != script->data->scriptGuid)
				continue;

			script->data = std::move(sourceData);
			script->needReload = true;
		}

		for (Entity entity : entityHandles)
		{
			auto renderModel = entity.tryGetComponent<RenderStaticModel>();
			if (!renderModel || !renderModel->model)
				continue;

			const auto& guid = renderModel->model->GetAssetGUID();
			if (guid.empty() || !resolveModelAsset)
				continue;
			auto model = resolveModelAsset(guid);
			if (!model || model->GetAssetGUID() != guid)
				continue;

			const bool hasOverrides = !renderModel->materialOverrideGUIDs.empty() || !renderModel->materialOverrideProperties.empty();
			auto instanceModel = hasOverrides ? model->Clone(false, false) : std::move(model);
			auto& meshInfos = instanceModel->GetMeshInfos();
			for (const auto& [index, overrideGuid] : renderModel->materialOverrideGUIDs)
			{
				if (index >= meshInfos.size())
					continue;
				auto& currentMaterial = meshInfos[index].material;
				if (currentMaterial && currentMaterial->GetAssetGUID() == overrideGuid)
					continue;
				if (!resolveMaterialAsset)
					continue;
				auto overrideMaterial = resolveMaterialAsset(overrideGuid);
				if (overrideMaterial && overrideMaterial->GetAssetGUID() == overrideGuid)
					currentMaterial = std::move(overrideMaterial);
			}
			for (const auto& [index, properties] : renderModel->materialOverrideProperties)
			{
				if (index >= meshInfos.size())
					continue;
				auto& currentMaterial = meshInfos[index].material;
				if (currentMaterial && !currentMaterial->GetAssetGUID().empty())
					continue;
				currentMaterial = std::make_shared<Material>(properties);
			}
			renderModel->model = std::move(instanceModel);
		}

		for (Entity entity : entityHandles)
		{
			auto renderModel = entity.tryGetComponent<RenderSkinnedModel>();
			if (!renderModel || !renderModel->model)
				continue;

			const auto& guid = renderModel->model->GetAssetGUID();
			if (guid.empty() || !resolveSkinnedModelAsset)
				continue;
			auto model = resolveSkinnedModelAsset(guid);
			if (!model || model->GetAssetGUID() != guid)
				continue;

			const bool hasOverrides = !renderModel->materialOverrideGUIDs.empty() || !renderModel->materialOverrideProperties.empty();
			auto instanceModel = hasOverrides ? model->Clone(false, false) : std::move(model);
			auto& meshInfos = instanceModel->GetMeshInfos();
			for (const auto& [index, overrideGuid] : renderModel->materialOverrideGUIDs)
			{
				if (index >= meshInfos.size())
					continue;
				auto& currentMaterial = meshInfos[index].material;
				if (currentMaterial && currentMaterial->GetAssetGUID() == overrideGuid)
					continue;
				if (!resolveMaterialAsset)
					continue;
				auto overrideMaterial = resolveMaterialAsset(overrideGuid);
				if (overrideMaterial && overrideMaterial->GetAssetGUID() == overrideGuid)
					currentMaterial = std::move(overrideMaterial);
			}
			for (const auto& [index, properties] : renderModel->materialOverrideProperties)
			{
				if (index >= meshInfos.size())
					continue;
				auto& currentMaterial = meshInfos[index].material;
				if (currentMaterial && !currentMaterial->GetAssetGUID().empty())
					continue;
				currentMaterial = std::make_shared<Material>(properties);
			}
			renderModel->model = std::move(instanceModel);
		}

		// 动画组：骨架和 clip 都是独立资产，按 GUID 取回来才建 Animator。
		for (Entity entity : entityHandles)
		{
			auto group = entity.tryGetComponent<SkeletonAnimatorGroup>();
			if (!group)
				continue;

			if (!group->skeletonGuid.empty() && resolveSkeletonAsset)
			{
				auto skeleton = resolveSkeletonAsset(group->skeletonGuid);
				if (skeleton && !skeleton->nodes.empty())
					group->skeleton = std::move(skeleton);
			}
			if (!group->skeleton)
				continue;

			for (auto& [name, data] : group->animatorDatas)
			{
				if (!data || data->clipGuid.empty() || !resolveAnimationClipAsset)
					continue;

				auto clip = resolveAnimationClipAsset(data->clipGuid);
				if (!clip)
					continue;

				data->clip = std::move(clip);
				data->animator = std::make_shared<Animator>(data->clip, group->skeleton);
			}
		}

		// 粒子发射器：材质和模型也是独立资产，按 GUID 解析后替换掉解码期只存了 GUID 的槽位。
		for (Entity entity : entityHandles)
		{
			auto* emitter = entity.tryGetComponent<ParticleEmitter>();
			if (!emitter)
				continue;

			auto& renderer = emitter->emitterParams.renderer;

			if (auto* billboardRenderer = std::get_if<ParticleBillboardRenderer>(&renderer))
			{
				if (!billboardRenderer->materialGuid.empty() && resolveMaterialAsset)
				{
					auto material = resolveMaterialAsset(billboardRenderer->materialGuid);
					if (material && material->GetAssetGUID() == billboardRenderer->materialGuid)
						billboardRenderer->material = std::move(material);
					else
						billboardRenderer->material.reset();
				}
			}
			else if (auto* meshRenderer = std::get_if<ParticleMeshRenderer>(&renderer))
			{
				// Custom：模型自带几何与材质
				if (!meshRenderer->modelGuid.empty() && resolveModelAsset)
				{
					auto model = resolveModelAsset(meshRenderer->modelGuid);
					if (model && model->GetAssetGUID() == meshRenderer->modelGuid)
						meshRenderer->model = std::move(model);
					else
						meshRenderer->model.reset();
				}

				// Cube / Sphere：内置网格，只解析可选材质
				if (!meshRenderer->materialGuid.empty() && resolveMaterialAsset)
				{
					auto material = resolveMaterialAsset(meshRenderer->materialGuid);
					if (material && material->GetAssetGUID() == meshRenderer->materialGuid)
						meshRenderer->material = std::move(material);
					else
						meshRenderer->material.reset();
				}
			}
		}
	}
	catch (const std::exception&)
	{
		return false;
	}

	return true;
}
