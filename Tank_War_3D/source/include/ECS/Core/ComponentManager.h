#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <iostream>
#include "Types.h"
#include "ComponentArray.h"

using ComponentMask = std::bitset<MAX_COMPONENTS>;

class ComponentManager
{
private:
	// 存储所有组件数组的映射
	mutable std::unordered_map<ComponentTypeID, std::shared_ptr<void>> componentArrays;

	// 类型信息映射
	std::unordered_map<ComponentTypeID, const char*> typeNames;

public:
	template<typename T>
	void registerComponent() {
		ComponentTypeID typeId = ComponentType<T>::getId();
		assert(componentArrays.find(typeId) == componentArrays.end() &&
			"Component type already registered!");
		componentArrays[typeId] = std::make_shared<ComponentArray<T>>();
		typeNames[typeId] = typeid(T).name();
	}

	template<typename T, typename... Args>
	T& addComponent(const Entity& entity, Args&&... args) {
		ComponentTypeID typeId = ComponentType<T>::getId();
		if (componentArrays.find(typeId) == componentArrays.end())
			registerComponent<T>();
		auto array = std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
		return array->addComponent(entity, std::forward<Args>(args)...);
	}

	template<typename T>
	void removeComponent(const Entity& entity)
	{
		ComponentTypeID typeId = ComponentType<T>::getId();
		assert(componentArrays.find(typeId) != componentArrays.end() &&
			"ComponentArray do not exist!");
		auto array = std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
		array->removeComponent(entity);
	}

	template<typename T>
	T& getComponent(const EntityID& entityId)
	{
		ComponentTypeID typeId = ComponentType<T>::getId();
		assert(componentArrays.find(typeId) != componentArrays.end() &&
			"ComponentArray do not exist!");
		auto array = std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
		return array->getComponent(entityId);
	}

	template<typename T>
	T* tryGetComponent(const EntityID& entityId)
	{
		ComponentTypeID typeId = ComponentType<T>::getId();
		if (componentArrays.find(typeId) == componentArrays.end())
			return nullptr;

		auto array = std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
		return array->tryGetComponent(entityId);
	}

	template<typename T>
	bool hasComponent(const EntityID& entityId) const
	{
		ComponentTypeID typeId = ComponentType<T>::getId();
		if (componentArrays.find(typeId) == componentArrays.end())
			return false;
		auto array = std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeId]);
		return array->hasComponent(entityId);
	}

	void entityDestroyed(const Entity& entity)
	{
		for (auto& pair : componentArrays)
		{
			auto array = std::static_pointer_cast<IComponentArray>(pair.second);
			array->removeComponent(entity);
		}
	}

	void printRegisteredComponents() const {
		std::cout << "Registered Components:\n";
		for (const auto& pair : typeNames) {
			std::cout << "  ID: " << pair.first << " -> " << pair.second << "\n";
		}
	}
};