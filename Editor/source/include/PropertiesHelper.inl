#include "PropertiesHelper.h"

// 注册组件
template<typename T>
void PropertiesHelper::RegisterComponent(const std::string& displayName)
{
	auto& registry = GetRegistry();
	registry[ComponentType<T>::getId()] = [](Entity entity, IComponent* comp) {
		if (!entity || !comp)
			return;

		auto& component = *(static_cast<T*>(comp));
		T temp = component;
		bool changed = DrawComponent(temp);
		if (changed)
		{
			auto world = entity.getWorld();
			world->SubmitCommand([entity, temp, &component]->void {
				component = temp;
				OnComponentChange(entity, component);
				});
		}
		};

	auto& names = GetComponentNames();
	names[ComponentType<T>::getId()] = displayName;
}

// 通用字段绘制
template<typename FieldType>
bool PropertiesHelper::DrawField(const std::string& name, FieldType* value)
{
	if constexpr (std::is_same_v<FieldType, float>) {
		return ImGui::DragFloat(name.c_str(), value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, int>) {
		return ImGui::DragInt(name.c_str(), value, 1);
	}
	else if constexpr (std::is_same_v<FieldType, uint32_t>) {
		int temp = static_cast<int>(*value);
		if (ImGui::DragInt(name.c_str(), &temp, 1, 0, 1000000)) {
			if (temp >= 0) {
				*value = static_cast<uint32_t>(temp);
				return true;
			}
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, bool>) {
		return ImGui::Checkbox(name.c_str(), value);
	}
	else if constexpr (std::is_same_v<FieldType, std::string>) {
		char buffer[256];
		strcpy_s(buffer, value->c_str());
		if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer))) {
			*value = buffer;
			return true;
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec3>) {
		return ImGui::DragFloat3(name.c_str(), (float*)value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec2>) {
		return ImGui::DragFloat2(name.c_str(), (float*)value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec4>) {
		return ImGui::DragFloat4(name.c_str(), (float*)value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::quat>) {
		glm::vec3 euler = glm::degrees(glm::eulerAngles(*value));
		if (ImGui::DragFloat3(name.c_str(), (float*)&euler, 1.f)) {
			*value = glm::quat(glm::radians(euler));
			return true;
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, ImColor>) {
		return ImGui::ColorEdit4(name.c_str(), (float*)value);
	}
	else {
		ImGui::Text("%s: [%s]", name.c_str(), typeid(FieldType).name());
		return false;
	}
}

template<>
inline bool PropertiesHelper::DrawField<Physics::BodyType>(const std::string& name, Physics::BodyType* value)
{
	int currentItem = static_cast<int>(*value);
	const char* items[] = { "Static", "Dynamic", "Kinematic" };
	if (ImGui::Combo(name.c_str(), &currentItem, items, IM_ARRAYSIZE(items))) {
		*value = static_cast<Physics::BodyType>(currentItem);
		return true;
	}
	return false;
}

template<>
inline bool PropertiesHelper::DrawField<VariableMaterialData::AlphaMode>(const std::string& name, VariableMaterialData::AlphaMode* value)
{
	int currentItem = static_cast<int>(*value);
	const char* items[] = { "Opaque", "Blend", "Mask" };
	if (ImGui::Combo(name.c_str(), &currentItem, items, IM_ARRAYSIZE(items))) {
		*value = static_cast<VariableMaterialData::AlphaMode>(currentItem);
		return true;
	}
	return false;
}

template <typename T, typename = void>
struct has_reflector : std::false_type {};

template <typename T>
struct has_reflector<T, std::void_t<typename rfl::Reflector<T>::ReflType>>
	: std::true_type {
};

template<typename T>
bool PropertiesHelper::DrawComponent(T& comp)
{
	bool anyChanged = false;

	if constexpr (has_reflector<T>::value) {
		using ReflType = typename rfl::Reflector<T>::ReflType;
		ReflType refl_obj = rfl::Reflector<T>::from(comp);// 从组件创建反射对象

		const auto fields = rfl::fields<ReflType>();// 获取字段信息
		auto view = rfl::to_view(refl_obj);

		// 遍历所有字段并绘制
		[&] <std::size_t... I>(std::index_sequence<I...>) {
			((anyChanged |= [&]() -> bool {
				const std::string& field_name = std::get<I>(fields).name();
				auto* ptr = view.template get<I>();
				if constexpr (std::is_pointer_v<decltype(ptr)>) {
					using FieldType = std::remove_pointer_t<decltype(ptr)>;
					bool changed = DrawField(field_name, ptr);
					return changed;
				}
				return false;
				}()), ...);
		}(std::make_index_sequence<fields.size()>{});

		// 如果有任何字段被修改，将反射对象写回组件
		if (anyChanged) {
			rfl::Reflector<T>::modify(comp, refl_obj);
		}

		return anyChanged;
	}
	else {
		const auto fields = rfl::fields<T>();
		auto view = rfl::to_view(comp);

		[&] <std::size_t... I>(std::index_sequence<I...>) {
			((anyChanged |= [&]() -> bool {
				const std::string& field_name = std::get<I>(fields).name();
				auto* ptr = view.template get<I>();
				if constexpr (std::is_pointer_v<decltype(ptr)>) {
					using FieldType = std::remove_pointer_t<decltype(ptr)>;
					bool changed = DrawField(field_name, ptr);
					return changed;
				}
				return false;
				}()), ...);
		}(std::make_index_sequence<fields.size()>{});

	}

	return anyChanged;
}

template<>
inline bool PropertiesHelper::DrawComponent(Physics& physics)
{
	bool anyChanged = false;

	anyChanged |= DrawField("bodyType", &physics.bodyType);
	anyChanged |= DrawField("mass", &physics.mass);
	anyChanged |= DrawField("friction", &physics.friction);
	anyChanged |= DrawField("rollingFriction", &physics.rollingFriction);
	anyChanged |= DrawField("restitution", &physics.restitution);
	anyChanged |= DrawField("isSensor", &physics.isSensor);
	anyChanged |= DrawField("fixedRotation", &physics.fixedRotation);
	anyChanged |= DrawField("isBullet", &physics.isBullet);
	anyChanged |= DrawField("isCharacter", &physics.isCharacter);
	anyChanged |= DrawField("allowSleep", &physics.allowSleep);

	if (physics.isCharacter) {
		ImGui::Indent();
		anyChanged |= DrawField("stepHeight", &physics.stepHeight);
		anyChanged |= DrawField("walkSpeed", &physics.walkSpeed);
		anyChanged |= DrawField("jumpSpeed", &physics.jumpSpeed);
		anyChanged |= DrawField("maxSlope", &physics.maxSlope);
		anyChanged |= DrawField("maxPenetrationDepth", &physics.maxPenetrationDepth);
		ImGui::Unindent();
	}

	return anyChanged;
}

template<>
inline bool PropertiesHelper::DrawComponent<RenderLight>(RenderLight& light)
{
	bool anyChanged = false;

	// 绘制 LightType 选择
	int typeIdx = static_cast<int>(light.type);
	const char* typeNames[] = { "Directional", "Point", "Spot" };
	if (ImGui::Combo("Light Type", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
		light.type = static_cast<LightType>(typeIdx);
		// 切换类型时重置数据
		switch (light.type) {
		case LightType::Directional:
			light.data = DirectionalLightData();
			break;
		case LightType::Point:
			light.data = PointLightData();
			break;
		case LightType::Spot:
			light.data = SpotLightData();
			break;
		}
		anyChanged = true;
	}

	// 绘制通用属性
	anyChanged |= DrawField("Render Cube", &light.renderCube);

	// 根据类型绘制对应的光照数据
	switch (light.type) {
	case LightType::Directional: {
		if (auto* dirData = light.GetData<DirectionalLightData>()) {
			anyChanged |= DrawComponent(*dirData);
		}
		break;
	}
	case LightType::Point: {
		if (auto* pointData = light.GetData<PointLightData>()) {
			anyChanged |= DrawComponent(*pointData);
		}
		break;
	}
	case LightType::Spot: {
		if (auto* spotData = light.GetData<SpotLightData>()) {
			anyChanged |= DrawComponent(*spotData);
		}
		break;
	}
	}

	return anyChanged;
}

template<typename T>
void PropertiesHelper::OnComponentChange(Entity entity, T& comp)
{
}

template<>
inline void PropertiesHelper::OnComponentChange(Entity entity, Transform& trans)
{
	if (auto* physics = entity.tryGetComponent<Physics>())
		physics->forceSyncTransform = true;
}

template<>
inline void PropertiesHelper::OnComponentChange(Entity entity, Physics& physics)
{
	physics.forceRecalculate = true;
}