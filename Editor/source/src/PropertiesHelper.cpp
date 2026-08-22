#pragma once

#include "PropertiesHelper.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "GameRuntimeComponents.h"
#include <rfl.hpp>
#include <rfl/json.hpp>

// 在程序初始化时注册所有组件
static void RegisterAllComponents()
{
	PropertiesHelper::RegisterComponent<Transform>("Transform");
	PropertiesHelper::RegisterComponent<Physics>("Physics");
	PropertiesHelper::RegisterComponent<Movement>("Movement");
	PropertiesHelper::RegisterComponent<RenderLight>("Light");
	PropertiesHelper::RegisterComponent<VariableMaterial>("Material");
	PropertiesHelper::RegisterComponent<CameraComponent>("Camera");
	PropertiesHelper::RegisterComponent<TagFreeCamera>("FreeCameraProp");
}

std::once_flag init_flag;
static void Need()
{
	std::call_once(init_flag, RegisterAllComponents);
}

std::vector<ComponentData> PropertiesHelper::GetAllComponents(Entity entity)
{
	Need();

	std::vector<ComponentData> result;

	for (auto& [typeID, drawFunc] : GetRegistry())
	{
		IComponent* comp = entity.tryGetComponentByTypeID(typeID);
		if (comp)
		{
			auto& names = GetComponentNames();
			auto it = names.find(typeID);
			if (it != names.end())
			{
				result.push_back({ typeID,it->second, comp });
			}
		}
	}

	return result;
}

void PropertiesHelper::DrawAllProperties(Entity entity, const std::vector<ComponentData>& components)
{
	Need();

	for (const auto& data : components)
	{
		if (!data.component)
			continue;

		if (ImGui::CollapsingHeader(data.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushID(data.name.c_str());
			auto& registry = GetRegistry();
			auto it = registry.find(data.typeID);
			if (it != registry.end())
			{
				it->second(entity, data.component);
			}
			ImGui::PopID();
		}
	}

	auto fields = rfl::fields<Transform>();

	for (auto& field : fields)
	{
		std::string fieldName = field.name();

	}
}

std::unordered_map<ComponentTypeID, PropertiesHelper::DrawFunction>& PropertiesHelper::GetRegistry()
{
	static std::unordered_map<ComponentTypeID, PropertiesHelper::DrawFunction> registry;
	return registry;
}

std::unordered_map<ComponentTypeID, std::string>& PropertiesHelper::GetComponentNames()
{
	static std::unordered_map<ComponentTypeID, std::string> names;
	return names;
}
