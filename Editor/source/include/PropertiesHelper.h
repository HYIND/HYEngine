#pragma once

#include <unordered_map>
#include <functional>
#include <vector>
#include <string>
#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"

#include "Reflect.h"

struct ComponentData
{
	ComponentTypeID typeID;
	std::string name;
	IComponent* component = nullptr;
};

class PropertiesHelper
{
public:
	using DrawFunction = std::function<void(Entity, IComponent*)>;

	// 注册组件
	template<typename T>
	static void RegisterComponent(const std::string& displayName);
	static std::vector<ComponentData> GetAllComponents(Entity entity);// 获取实体的所有组件
	static void DrawAllProperties(Entity entity, const std::vector<ComponentData>& components);// 绘制所有组件

	// 通用绘制模板
	template<typename T>
	static bool DrawData(T& data) { return DrawComponent(data); }

	template<typename T>
	static bool DrawWithTitle(const std::string& title, T& data);

private:
	static std::unordered_map<ComponentTypeID, DrawFunction>& GetRegistry();
	static std::unordered_map<ComponentTypeID, std::string>& GetComponentNames();

	// 通用绘制模板
	template<typename T>
	static bool DrawComponent(T& comp);

	// 默认修改回调
	template<typename T>
	static void OnComponentChange(Entity entity, T& comp);

	// 字段绘制辅助
	template<typename FieldType>
	static bool DrawField(const std::string& name, FieldType* value);
};

template<typename T>
bool PropertiesHelper::DrawWithTitle(const std::string& title, T& data)
{
	if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushID(title.c_str());
		bool result = PropertiesHelper::DrawData(data);
		ImGui::PopID();
		return result;
	}
	return false;
};


#include "PropertiesHelper.inl"