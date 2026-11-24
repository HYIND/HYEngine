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
	// 直接注册自定义绘制函数（面板需要实体信息、不走快照 / 反射的组件用）
	static void RegisterCustomDraw(ComponentTypeID typeID, DrawFunction draw) { GetCustomDrawRegistry()[typeID] = std::move(draw); }
	static std::vector<ComponentData> GetAllComponents(Entity entity);// 获取实体的所有组件
	static void DrawAllProperties(Entity entity, const std::vector<ComponentData>& components, ProjectManager* assetManager);

	// 通用绘制模板
	template<typename T>
	static bool DrawData(T& data) { return DrawComponent(data); }

	template<typename T>
	static bool DrawWithTitle(const std::string& title, T& data);

	static void SetNextPropertyFieldWidth(float maxWidth, float fraction = 0.5f);

	// ── 恢复默认值（渲染参数面板用）──
	// 默认值 = 参数结构的默认构造（T{}，即 RenderOption.h 里的成员初始值）。bool 是功能开关，不参与重置。
	// 逐字段绘制；非 bool 字段的值与默认值不同时，后面显示「重置」小按钮（悬停显示默认值）
	template<typename T>
	static bool DrawDataResettable(T& data);
	// 整体恢复为默认值（T{}，含 bool 开关）。返回是否有改动
	template<typename T>
	static bool ResetValues(T& data);
	// 值与默认值不同时，在同一行画一个「重置」小按钮；点了就恢复默认值并返回 true。bool 不画
	template<typename V>
	static bool ResetButton(const char* id, V& value, const V& defaultValue);

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
	
	// 静态/蒙皮模型组件的属性面板几乎一样，只有模型资产的类型名不同。
	template<typename ModelComponent>
	static bool DrawModelComponent(ModelComponent& comp, ProjectManager* assetManager, const char* modelLabel);
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
