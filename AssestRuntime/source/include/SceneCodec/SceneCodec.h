#pragma once

#include "ComponentCodec.h"
#include "Script/ScriptSourceData.h"

class StaticModel;
class SkinnedModel;
class Material;
struct Skeleton;
class AnimationClip;

class SceneCodec
{
	using Json = nlohmann::json;

public:
	static constexpr uint32_t CurrentFormatVersion = 1;

	bool LoadFromJson(const Json& content);
	Json SaveToJson() const;

	bool Serialize(World& world, const std::function<bool(Entity)>& includeEntity = {});
	bool Deserialize(
		World& world,
		const std::function<std::shared_ptr<ScriptSourceData>(const AssetGUID&)>& resolveScriptAsset = {},
		const std::function<std::shared_ptr<StaticModel>(const AssetGUID&)>& resolveModelAsset = {},
		const std::function<std::shared_ptr<SkinnedModel>(const AssetGUID&)>& resolveSkinnedModelAsset = {},
		const std::function<std::shared_ptr<Skeleton>(const AssetGUID&)>& resolveSkeletonAsset = {},
		const std::function<std::shared_ptr<AnimationClip>(const AssetGUID&)>& resolveAnimationClipAsset = {},
		const std::function<std::shared_ptr<Material>(const AssetGUID&)>& resolveMaterialAsset = {}) const;

	const std::string& GetName() const { return _name; }
	void SetName(const std::string& name) { _name = name; }

	Json& GetSettings() { return _settings; }
	const Json& GetSettings() const { return _settings; }

	std::vector<NComponentCodec::EntityData>& GetEntities() { return _entities; }
	const std::vector<NComponentCodec::EntityData>& GetEntities() const { return _entities; }

private:
	std::string _name;
	Json _settings = Json::object();
	std::vector<NComponentCodec::EntityData> _entities;
};
