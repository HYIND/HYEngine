#pragma once

#include "stdafx.h"

#include <unordered_map>
#include <functional>
#include <vector>
#include <string>
#include <algorithm>

#include "Reflect.h"
#include "Project/ProjectManager.h"

class ProjectManager;

struct ComponentData
{
	ComponentTypeID typeID;
	std::string name;
	IComponent* component = nullptr;
	bool removable = true;
};

class PropertiesHelper
{
public:
	using DrawFunction = std::function<void(Entity, IComponent*, ProjectManager*)>;
	using RemoveFunction = std::function<bool(Entity)>;

	// 注册组件
	template<typename T>
	static void RegisterComponent();
	static std::vector<ComponentData> GetAllComponents(Entity entity);// 获取实体的所有组件
	static void DrawAllProperties(Entity entity, const std::vector<ComponentData>& components, ProjectManager* assetManager);

	// 通用绘制模板
	template<typename T>
	static bool DrawData(T& data) { return DrawComponent(data); }

	template<typename T>
	static bool DrawWithTitle(const std::string& title, T& data);

private:
	static std::unordered_map<ComponentTypeID, DrawFunction>& GetCustomDrawRegistry();

	// 通用绘制模板
	template<typename T>
	static bool DrawComponent(T& comp);
	template<typename T>
	static bool DrawComponent(T& comp, ProjectManager*) { return DrawComponent(comp); }

	// 默认修改回调
	template<typename T>
	static void OnComponentChange(Entity entity, T& comp);

	// 特定Owner类型的特定字段绘制，由Reflect中的PropertyUI决定绘制的PropertyWidget
	template<typename Owner, typename FieldType>
	static bool DrawPropertyField(const std::string& name, FieldType* value);

	// 字段绘制辅助
	template<typename FieldType>
	static bool DrawField(const std::string& name, FieldType* value, PropertyWidget widget);

	static void SetNextPropertyFieldWidth(float maxWidth, float fraction = 0.5f);
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
