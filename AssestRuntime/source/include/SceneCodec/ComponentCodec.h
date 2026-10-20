#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include "ECSCore/World.h"
#include "ECSCore/ComponentTypes.h"
#include <nlohmann/json.hpp>


namespace NComponentCodec
{
	using Json = nlohmann::json;

	struct ComponentData
	{
		std::string type;
		Json data = Json::object();
	};

	struct EntityData
	{
		std::string id;
		std::string name;
		std::string parentId;
		std::vector<ComponentData> components;
	};

	struct ComponentCodec
	{
		std::string type;
		ComponentTypeID componentType = 0;
		std::function<bool(Entity)> has;
		std::function<Json(Entity)> encode;
		std::function<bool(Entity, const Json&)> decode;
		std::function<bool(Entity, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences;
		std::function<std::string(Entity)> getName;
		std::function<void(Entity, const std::string&)> setName;
	};

	std::vector<ComponentCodec>& GetComponentCodecs();
	void RegisterCodec(ComponentCodec codec);
	void RegisterBuiltInCodecs();

	template<typename T>
	void RegisterComponentCodec(
		const std::string& type,
		std::function<Json(const T&)> encode,
		std::function<bool(T&, const Json&)> decode,
		std::function<std::string(const T&)> getName = {},
		std::function<void(T&, const std::string&)> setName = {},
		std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences = {});

}

template<typename T>
void NComponentCodec::RegisterComponentCodec(
	const std::string& type,
	std::function<Json(const T&)> encode,
	std::function<bool(T&, const Json&)> decode,
	std::function<std::string(const T&)> getName,
	std::function<void(T&, const std::string&)> setName,
	std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences)
{
	ComponentCodec codec;
	codec.type = type;
	codec.componentType = ComponentType<T>::getId();
	codec.has = [](Entity entity) { return entity.hasComponent<T>(); };
	codec.encode = [encode = std::move(encode)](Entity entity) {
		return encode(entity.getComponent<T>());
		};
	if (resolveReferences)
		codec.resolveReferences = [resolveReferences = std::move(resolveReferences)](Entity entity, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
		return resolveReferences(entity.getComponent<T>(), data, entities);
		};
	codec.decode = [decode = std::move(decode)](Entity entity, const Json& data) {
		T component{};
		if (!decode(component, data))
			return false;
		entity.addComponent<T>(std::move(component));
		return true;
		};
	if (getName)
		codec.getName = [getName = std::move(getName)](Entity entity) {
		return getName(entity.getComponent<T>());
		};
	if (setName)
		codec.setName = [setName = std::move(setName)](Entity entity, const std::string& name) {
		setName(entity.getComponent<T>(), name);
		};
	RegisterCodec(std::move(codec));
}