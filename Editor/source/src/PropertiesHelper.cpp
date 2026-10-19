
#include "PropertiesHelper.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "GameRuntimeComponents.h"
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <format>

// 在程序初始化时注册所有组件
static void RegisterAllComponents()
{
	PropertiesHelper::RegisterComponent<Transform>("Transform");
	PropertiesHelper::RegisterComponent<Physics>("Physics");
	PropertiesHelper::RegisterComponent<Movement>("Movement");
	PropertiesHelper::RegisterComponent<RenderLight>("Light");
	PropertiesHelper::RegisterComponent<CameraComponent>("Camera");
	PropertiesHelper::RegisterComponent<TagFreeCamera>("FreeCameraProp");
	PropertiesHelper::RegisterComponent<RenderModel>("Render Model");
	PropertiesHelper::RegisterComponent<Script>("Script");
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

void PropertiesHelper::DrawAllProperties(Entity entity, const std::vector<ComponentData>& components, ProjectManager* assetManager)
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
				it->second(entity, data.component, assetManager);
			ImGui::PopID();
		}
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

void PropertiesHelper::SetNextPropertyFieldWidth(float maxWidth, float fraction)
{
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	ImGui::SetNextItemWidth(std::min(maxWidth, availableWidth * fraction));
}
