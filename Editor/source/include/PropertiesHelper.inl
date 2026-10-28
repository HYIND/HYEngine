#include "PropertiesHelper.h"
#include "ImguiLayout.h"
#include "SceneCodec/ComponentCodec.h"

template <typename T>
struct EditorSnapshot
{
	static void Make(const T& src, T& snapshot) { snapshot = src; }
	static void Apply(T&& snapshot, T& dst) { dst = std::move(snapshot); }
};

template <>
struct EditorSnapshot<UIView>
{
	static void Make(const UIView& src, UIView& snapshot)
	{
		snapshot.documentGuid = src.documentGuid;
		snapshot.viewName = src.viewName;
		snapshot.layer = src.layer;
		snapshot.capturesInput = src.capturesInput;
		snapshot.enabled = src.enabled;
		snapshot.needReload = src.needReload;
	}

	static void Apply(UIView&& snapshot, UIView& dst)
	{
		dst.documentGuid = snapshot.documentGuid;
		dst.viewName = snapshot.viewName;
		dst.layer = snapshot.layer;
		dst.capturesInput = snapshot.capturesInput;
		dst.enabled = snapshot.enabled;
		dst.needReload = snapshot.needReload;
	}
};

template <>
struct EditorSnapshot<Script>
{
	static void Make(const Script& src, Script& snapshot)
	{
		snapshot.data = src.data;
		snapshot.enabled = src.enabled;
		snapshot.needReload = src.needReload;
	}

	static void Apply(Script&& snapshot, Script& dst)
	{
		dst.data = snapshot.data;
		dst.enabled = snapshot.enabled;
		dst.needReload = snapshot.needReload;
	}
};

// 注册组件
template<typename T>
void PropertiesHelper::RegisterComponent()
{
	const ComponentTypeID typeID = ComponentType<T>::getId();
	auto& codecs = NComponentCodec::GetComponentCodecs();
	auto codec = std::find_if(codecs.begin(), codecs.end(), [typeID](const auto& item) {
		return item.componentType == typeID;
		});
	if (codec != codecs.end())
	{
		auto& registry = GetCustomDrawRegistry();
		registry[ComponentType<T>::getId()] = [](Entity entity, IComponent* comp, ProjectManager* assetManager) {
			if (!entity || !comp)
				return;

			if constexpr (sizeof(T) > sizeof(IComponent))
			{
				T* temp = new T();
				auto& component = *(static_cast<T*>(comp));
				EditorSnapshot<T>::Make(component, *temp);
				bool changed = DrawComponent(*temp, assetManager);
				if (changed)
				{
					auto world = entity.getWorld();
					world->SubmitCommand([entity, temp, &component] mutable ->void {
						EditorSnapshot<T>::Apply(std::move(*temp), component);
						delete(temp);
						OnComponentChange(entity, component);
						});
				}
			}

			};
	}

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
	if (widget == PropertyWidget::Const)
	{
		ImGui::Text("%s:", name.c_str());
		ImGui::SameLine();

		if constexpr (std::is_same_v<FieldType, std::string>)
			ImGui::TextUnformatted(value->c_str());
		else if constexpr (std::is_same_v<FieldType, bool>)
			ImGui::TextUnformatted(*value ? "true" : "false");
		else if constexpr (std::is_integral_v<FieldType> && std::is_signed_v<FieldType>)
			ImGui::Text("%lld", static_cast<long long>(*value));
		else if constexpr (std::is_integral_v<FieldType> && std::is_unsigned_v<FieldType>)
			ImGui::Text("%llu", static_cast<unsigned long long>(*value));
		else if constexpr (std::is_floating_point_v<FieldType>)
			ImGui::Text("%.6g", static_cast<double>(*value));
		else if constexpr (std::is_same_v<FieldType, glm::vec2>)
			ImGui::Text("(%.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y));
		else if constexpr (std::is_same_v<FieldType, glm::vec3>)
			ImGui::Text("(%.4g, %.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y), static_cast<double>(value->z));
		else if constexpr (std::is_same_v<FieldType, glm::vec4>)
			ImGui::Text("(%.4g, %.4g, %.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y), static_cast<double>(value->z), static_cast<double>(value->w));
		else if constexpr (std::is_enum_v<FieldType>)
			ImGui::Text("%lld", static_cast<long long>(static_cast<std::underlying_type_t<FieldType>>(*value)));
		else
			ImGui::TextUnformatted("<read-only>");

		return false;
	}

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

		if constexpr (std::tuple_size_v<decltype(fields)> > 0)
		{
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
		else
		{
			return false;
		}

	}
	else {
		if constexpr (std::is_empty_v<T>)
		{
			return false;
		}
		else
		{
			const auto fields = rfl::fields<T>();

			if constexpr (std::tuple_size_v<decltype(fields)> > 0)
			{
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
			else
			{
				return false;
			}
		}
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
	anyChanged |= DrawPropertyField<RenderLight>("Render Light Shape", &light.renderCube);
	anyChanged |= DrawPropertyField<RenderLight>("Visible", &light.visible);

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

	bool anyChanged = false;
	anyChanged |= DrawPropertyField<RenderModel>("Visible", &comp.visible);

	struct MaterialEdit
	{
		size_t slot;
		MaterialProperties properties;
		std::shared_ptr<Material> material;
		AssetGUID guid;
	};
	struct MaterialDrop
	{
		size_t slot;
		std::shared_ptr<Material> material;
	};
	std::vector<MaterialEdit> edits;
	std::vector<MaterialDrop> drops;
	auto& meshInfos = comp.model->getMeshInfos();
	for (size_t slot = 0; slot < meshInfos.size(); ++slot)
	{
		ImGui::PushID(static_cast<int>(slot));
		const std::string title = std::format("Material Slot {}", slot);
		if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_None))
		{
			auto& material = meshInfos[slot].material;
			ImGui::TextUnformatted("Material");
			ImGui::SameLine();
			auto database = assetManager ? assetManager->GetDatabase() : nullptr;
			auto materialMeta = database && material ? database->GetAssetMetaByGuid(material->GetAssetGUID()) : nullptr;
			const std::string materialLabel = materialMeta ? materialMeta->name :
				(material && !material->GetAssetGUID().empty() ? material->GetAssetGUID() : "No material assigned");
			ImGui::Button(materialLabel.c_str(), ImVec2(220.f, 0.f));
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
				&& materialMeta && materialMeta->type == AssetType::Material)
				ImguiLayout::OpenMaterialEditor(materialMeta->guid);
			if (ImGui::BeginDragDropTarget())
			{
				if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
				{
					const auto* guidData = static_cast<const char*>(payload->Data);
					if (guidData && guidData[0] != '\0' && database && assetManager)
					{
						auto droppedMeta = database->GetAssetMetaByGuid(AssetGUID(guidData));
						if (droppedMeta && droppedMeta->type == AssetType::Material)
						{
							auto asset = assetManager->LoadAsset(droppedMeta->guid);
							auto replacement = asset ? asset->AsMaterial() : nullptr;
							if (replacement)
								drops.push_back({ slot, std::move(replacement) });
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (!material)
				ImGui::TextDisabled("No material assigned");
			else
			{
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
					edits.push_back({ slot, properties, material, material->GetAssetGUID() });

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

	std::shared_ptr<Model> instanceModel;
	for (const auto& edit : edits)
	{
		if (!edit.guid.empty())
		{
			if (edit.material)
				edit.material->SetProperty(edit.properties);
			if (assetManager)
			{
				auto asset = assetManager->LoadAsset(edit.guid);
				auto materialAsset = asset ? std::dynamic_pointer_cast<MaterialAsset>(asset) : nullptr;
				if (materialAsset)
				{
					materialAsset->SetProperties(edit.properties);
					assetManager->SaveAsset(edit.guid);
				}
			}
			continue;
		}
		else
		{
			if (!instanceModel)
				instanceModel = comp.model->Clone(false, false, false);
			auto& editedMeshInfos = instanceModel->getMeshInfos();
			if (edit.slot >= editedMeshInfos.size())
				continue;
			auto materialInstance = editedMeshInfos[edit.slot].material
				? editedMeshInfos[edit.slot].material->Clone()
				: std::make_shared<Material>();
			materialInstance->SetProperty(edit.properties);
			editedMeshInfos[edit.slot].material = std::move(materialInstance);
			const uint32_t index = static_cast<uint32_t>(edit.slot);
			comp.materialOverrideGUIDs.erase(index);
			comp.materialOverrideProperties[index] = edit.properties;
		}
	}

	if (!drops.empty())
	{
		if (!instanceModel)
			instanceModel = comp.model->Clone(false, false, false);
		auto& editedMeshInfos = instanceModel->getMeshInfos();
		for (const auto& drop : drops)
		{
			if (drop.slot >= editedMeshInfos.size())
				continue;
			editedMeshInfos[drop.slot].material = drop.material;
			const uint32_t index = static_cast<uint32_t>(drop.slot);
			const auto& guid = drop.material->GetAssetGUID();
			if (guid.empty())
			{
				comp.materialOverrideGUIDs.erase(index);
				comp.materialOverrideProperties[index] = drop.material->GetProperties();
			}
			else
			{
				comp.materialOverrideGUIDs[index] = guid;
				comp.materialOverrideProperties.erase(index);
			}
		}
	}
	if (instanceModel)
		comp.model = std::move(instanceModel);

	return anyChanged || !edits.empty() || !drops.empty();
}

template<>
inline bool PropertiesHelper::DrawComponent<Script>(Script& comp, ProjectManager* assetManager)
{
	bool changed = false;
	if (!comp.data)
	{
		ImGui::TextDisabled("No Script assigned");
	}
	else
	{
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

		drawAssetPath("ScriptSource", comp.data->scriptGuid);
	}

	changed |= ImGui::Checkbox("Enabled", &comp.enabled);
	ImGui::SameLine();
	if (ImGui::Button("Reload"))
	{
		auto reloadScript = [&]() -> bool
			{
				if (!assetManager || !comp.data || comp.data->scriptGuid.empty())
					return false;

				auto& guid = comp.data->scriptGuid;
				if (auto meta = assetManager->GetDatabase()->GetAssetMetaByGuid(guid))
				{
					if (meta->type != AssetType::Script)
						return false;

					auto asset = assetManager->LoadAsset(guid);
					auto source = asset ? asset->AsScript() : nullptr;
					if (!source)
						return false;

					comp.data = source;
					return true;
				}
			};

		reloadScript();
		comp.needReload = true;
		changed |= true;
	}
	if (ImGui::Button("Drop script asset to replace", ImVec2(-FLT_MIN, 0.0f))) {}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && *data && assetManager)
			{
				auto database = assetManager->GetDatabase();
				auto meta = database ? database->GetAssetMetaByGuid(AssetGUID(data)) : nullptr;
				if (meta && meta->type == AssetType::Script)
				{
					auto asset = assetManager->LoadAsset(meta->guid);
					auto source = asset ? asset->AsScript() : nullptr;
					if (source)
					{
						comp.data = std::move(source);
						comp.needReload = true;
						changed = true;
					}
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && strlen(data) > 0)
			{
				std::string fileFullPath(data);
				std::string ext = fs::path(fileFullPath).extension().string();
				if (ext == ".lua")
				{
					if (assetManager)
						ImguiLayout::HandleScriptFileDrop(fileFullPath, ImguiLayout::selectedEntity, assetManager);
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<UIView>(UIView& comp, ProjectManager* assetManager)
{
	bool changed = false;

	auto database = assetManager ? assetManager->GetDatabase() : nullptr;
	auto meta = (database && !comp.documentGuid.empty()) ? database->GetAssetMetaByGuid(comp.documentGuid) : nullptr;

	ImGui::TextUnformatted("UI Document");
	ImGui::SameLine();
	if (comp.documentGuid.empty())
	{
		ImGui::TextDisabled("Unlinked");
	}
	else if (!meta)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Missing asset (%s)", comp.documentGuid.c_str());
	}
	else
	{
		ImGui::TextUnformatted(meta->name.c_str());
		ImGui::TextDisabled("%s", meta->path.c_str());
	}

	{
		char buffer[256]{};
		strncpy_s(buffer, comp.viewName.c_str(), _TRUNCATE);
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		if (ImGui::InputText("View Name", buffer, sizeof(buffer), ImGuiInputTextFlags_AutoSelectAll))
		{
			comp.viewName = buffer;
			changed = true;
		}
		if (comp.viewName.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Unset View Name can't be find by module");
		}
	}

	changed |= ImGui::Checkbox("Enabled", &comp.enabled);
	changed |= ImGui::Checkbox("Captures Input", &comp.capturesInput);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragInt("Layer", &comp.layer, 0.1f, -10000, 10000);
	if (ImGui::Button("Reload"))
	{
		comp.needReload = true;
		changed = true;
	}

	// 和脚本组件一样：可以把资产面板里的 .ui 拖到这个按钮上替换。
	if (ImGui::Button("Drop UI document to replace", ImVec2(-FLT_MIN, 0.0f))) {}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && *data && assetManager)
			{
				auto droppedMeta = database ? database->GetAssetMetaByGuid(AssetGUID(data)) : nullptr;
				if (droppedMeta && droppedMeta->type == AssetType::UiDocument)
				{
					comp.documentGuid = droppedMeta->guid;
					comp.needReload = true;
					changed = true;
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && strlen(data) > 0)
			{
				std::string fileFullPath(data);
				std::string ext = fs::path(fileFullPath).extension().string();
				if (ext == ".ui" && assetManager)
					ImguiLayout::HandleUIAssetFileDrop(fileFullPath, ImguiLayout::selectedEntity, assetManager);
			}
		}
		ImGui::EndDragDropTarget();
	}

	return changed;
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
