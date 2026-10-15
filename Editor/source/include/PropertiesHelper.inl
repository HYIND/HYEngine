#include "PropertiesHelper.h"

// 注册组件
template<typename T>
void PropertiesHelper::RegisterComponent(const std::string& displayName)
{
	auto& registry = GetRegistry();
	registry[ComponentType<T>::getId()] = [](Entity entity, IComponent* comp, ProjectManager* assetManager) {
		if (!entity || !comp)
			return;

		auto& component = *(static_cast<T*>(comp));
		T temp = component;
		bool changed = DrawComponent(temp, assetManager);
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

template<typename Owner, typename FieldType>
bool PropertiesHelper::DrawPropertyField(const std::string& name, FieldType* value)
{
	const auto widget = PropertyUI<Owner>::GetWidget(name);
	return DrawField(name, value, widget);
}

template<typename FieldType>
bool PropertiesHelper::DrawField(const std::string& name, FieldType* value, PropertyWidget widget)
{
	if constexpr (std::is_same_v<FieldType, float>) {
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		return ImGui::DragFloat(name.c_str(), value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, int>) {
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		return ImGui::DragInt(name.c_str(), value, 1);
	}
	else if constexpr (std::is_same_v<FieldType, uint32_t>) {
		int temp = static_cast<int>(*value);
		SetNextPropertyFieldWidth(130.0f, 0.55f);
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
		SetNextPropertyFieldWidth(220.0f, 0.6f);
		if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer))) {
			*value = buffer;
			return true;
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec3>) {
		if (widget == PropertyWidget::Color) {
			// 颜色字段
			SetNextPropertyFieldWidth(250.0f, 0.6f);
			return ImGui::ColorEdit3(name.c_str(), glm::value_ptr(*value));
		}
		else {
			// 普通 vec3 默认处理
			SetNextPropertyFieldWidth(250.0f, 0.6f);
			return ImGui::DragFloat3(name.c_str(), glm::value_ptr(*value), 0.1f);
		}
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec2>) {
		SetNextPropertyFieldWidth(180.0f, 0.55f);
		return ImGui::DragFloat2(name.c_str(), glm::value_ptr(*value), 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec4>) {
		SetNextPropertyFieldWidth(280.0f, 0.65f);
		return ImGui::DragFloat4(name.c_str(), glm::value_ptr(*value), 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::quat>) {
		glm::vec3 euler = glm::degrees(glm::eulerAngles(*value));
		SetNextPropertyFieldWidth(250.0f, 0.6f);
		if (ImGui::DragFloat3(name.c_str(), (float*)&euler, 1.f)) {
			*value = glm::quat(glm::radians(euler));
			return true;
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, ImColor>) {
		SetNextPropertyFieldWidth(180.0f, 0.55f);
		return ImGui::ColorEdit4(name.c_str(), (float*)value);
	}
	else {
		ImGui::Text("%s: [%s]", name.c_str(), typeid(FieldType).name());
		return false;
	}
}

template<>
inline bool PropertiesHelper::DrawField<Physics::BodyType>(const std::string& name, Physics::BodyType* value, PropertyWidget widget)
{
	int currentItem = static_cast<int>(*value);
	const char* items[] = { "Static", "Dynamic", "Kinematic" };
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::Combo(name.c_str(), &currentItem, items, IM_ARRAYSIZE(items))) {
		*value = static_cast<Physics::BodyType>(currentItem);
		return true;
	}
	return false;
}

template <typename T, typename = void>
struct has_reflector : std::false_type {};

template <typename T>
struct has_reflector<T, std::void_t<typename rfl::Reflector<T>::ReflType>>
	: std::true_type {};

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
					return DrawPropertyField<T>(field_name, ptr);
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
					return DrawPropertyField<T>(field_name, ptr);
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

	anyChanged |= DrawPropertyField<Physics>("bodyType", &physics.bodyType);
	anyChanged |= DrawPropertyField<Physics>("mass", &physics.mass);
	anyChanged |= DrawPropertyField<Physics>("friction", &physics.friction);
	anyChanged |= DrawPropertyField<Physics>("rollingFriction", &physics.rollingFriction);
	anyChanged |= DrawPropertyField<Physics>("restitution", &physics.restitution);
	anyChanged |= DrawPropertyField<Physics>("isSensor", &physics.isSensor);
	anyChanged |= DrawPropertyField<Physics>("fixedRotation", &physics.fixedRotation);
	anyChanged |= DrawPropertyField<Physics>("isBullet", &physics.isBullet);
	anyChanged |= DrawPropertyField<Physics>("isCharacter", &physics.isCharacter);
	anyChanged |= DrawPropertyField<Physics>("allowSleep", &physics.allowSleep);

	if (physics.isCharacter) {
		ImGui::Indent();
		anyChanged |= DrawPropertyField<Physics>("stepHeight", &physics.stepHeight);
		anyChanged |= DrawPropertyField<Physics>("walkSpeed", &physics.walkSpeed);
		anyChanged |= DrawPropertyField<Physics>("jumpSpeed", &physics.jumpSpeed);
		anyChanged |= DrawPropertyField<Physics>("maxSlope", &physics.maxSlope);
		anyChanged |= DrawPropertyField<Physics>("maxPenetrationDepth", &physics.maxPenetrationDepth);
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
	SetNextPropertyFieldWidth(130.0f, 0.55f);
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
	anyChanged |= DrawPropertyField<Physics>("Render Light Shape", &light.renderCube);

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

template<>
inline bool PropertiesHelper::DrawComponent<RenderModel>(RenderModel& comp, ProjectManager* assetManager)
{
	if (!comp.model)
	{
		ImGui::TextDisabled("No Model assigned");
		return false;
	}

	const auto drawAssetPath = [assetManager](const char* label, const AssetGUID& guid)
		{
			ImGui::Text("%s", label);
			ImGui::SameLine();
			if (guid.empty())
			{
				ImGui::TextDisabled("Unlinked");
				return;
			}

			auto database = assetManager ? assetManager->GetDatabase() : nullptr;
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (!meta)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Missing asset (%s)", guid.c_str());
				return;
			}

			ImGui::TextUnformatted(meta->name.c_str());
			ImGui::TextDisabled("%s", meta->path.c_str());
		};

	drawAssetPath("Model", comp.model->GetAssetGUID());

	struct MaterialEdit
	{
		size_t slot;
		MaterialProperties properties;
	};
	std::vector<MaterialEdit> edits;
	const auto& meshInfos = comp.model->getMeshInfos();
	for (size_t slot = 0; slot < meshInfos.size(); ++slot)
	{
		ImGui::PushID(static_cast<int>(slot));
		const std::string title = std::format("Material Slot {}", slot);
		if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_None))
		{
			auto material = meshInfos[slot].material;
			if (!material)
			{
				ImGui::TextDisabled("No material assigned");
			}
			else
			{
				drawAssetPath("Material", material->GetAssetGUID());
				auto properties = material->GetProperties();
				bool slotChanged = false;
				SetNextPropertyFieldWidth(250.f, 0.6f);
				slotChanged |= ImGui::ColorEdit3("Albedo", glm::value_ptr(properties.albedo));
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("Metallic", &properties.metallic, 0.01f, 0.0f, 1.0f);
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("Roughness", &properties.roughness, 0.01f, 0.0f, 1.0f);
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("Ambient Occlusion", &properties.ambientOcclusion, 0.01f, 0.0f, 1.0f);
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("Opacity", &properties.opacity, 0.01f, 0.0f, 1.0f);
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("IOR", &properties.IOR, 0.01f, 1.0f, 3.0f);
				SetNextPropertyFieldWidth(250.f, 0.6f);
				slotChanged |= ImGui::ColorEdit3("Emission Color", glm::value_ptr(properties.emissionColor));
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				slotChanged |= ImGui::DragFloat("Emission Strength", &properties.emissionStrength, 0.01f, 0.0f, 100.0f);

				int alphaMode = static_cast<int>(properties.alphamode);
				const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
				SetNextPropertyFieldWidth(130.0f, 0.55f);
				if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModes, IM_ARRAYSIZE(alphaModes)))
				{
					properties.alphamode = static_cast<AlphaMode>((uint32_t)alphaMode);
					slotChanged = true;
				}
				if (properties.alphamode == AlphaMode::Mask)
				{
					SetNextPropertyFieldWidth(130.0f, 0.55f);
					slotChanged |= ImGui::DragFloat("Mask Threshold", &properties.maskthreshold, 0.01f, 0.0f, 1.0f);
				}
				slotChanged |= ImGui::Checkbox("Two Sided", &properties.twosided);

				if (slotChanged)
					edits.push_back({ slot, properties });

				const auto textureName = [](TextureType type) -> const char*
					{
						switch (type)
						{
						case TextureType::Albedo: return "Albedo Texture";
						case TextureType::Metallic: return "Metallic Texture";
						case TextureType::Roughness: return "Roughness Texture";
						case TextureType::Normal: return "Normal Texture";
						case TextureType::AO: return "AO Texture";
						case TextureType::Emissive: return "Emissive Texture";
						case TextureType::MetallicRoughness: return "Metallic Roughness Texture";
						case TextureType::Height: return "Height Texture";
						case TextureType::Opacity: return "Opacity Texture";
						default: return "Texture";
						}
					};
				for (const auto& [type, texture] : material->GetTextures())
					if (texture)
						drawAssetPath(textureName(type), texture->GetAssetGUID());
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (!edits.empty())
	{
		auto& editedMeshInfos = comp.model->getMeshInfos();
		for (const auto& edit : edits)
			if (edit.slot < editedMeshInfos.size() && editedMeshInfos[edit.slot].material)
				editedMeshInfos[edit.slot].material->SetProperty(edit.properties);
		return true;
	}

	return false;
}

template<typename T>
void PropertiesHelper::OnComponentChange(Entity entity, T& comp)
{}

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
