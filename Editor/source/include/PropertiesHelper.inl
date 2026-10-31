#include "PropertiesHelper.h"
#include "ImguiLayout.h"
#include "SceneCodec/ComponentCodec.h"
#include "Render/Components/ParticleEmitter.h"
#include <algorithm>
#include <cstdio>

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
				struct EditorBuffer
				{
					EntityID entityId = 0;
					const void* source = nullptr;
					T* buffer = nullptr;
				};
				static EditorBuffer cache;

				auto& component = *(static_cast<T*>(comp));
				if (cache.buffer == nullptr)
					cache.buffer = new T();

				if (cache.entityId != entity.getId() || cache.source != comp)
				{
					EditorSnapshot<T>::Make(component, *cache.buffer);
					cache.entityId = entity.getId();
					cache.source = comp;
				}

				bool changed = DrawComponent(*cache.buffer, assetManager);
				if (changed)
				{
					auto world = entity.getWorld();
					T* buffer = cache.buffer;
					world->SubmitCommand([entity, buffer, &component] mutable ->void {
						T value = *buffer;
						EditorSnapshot<T>::Apply(std::move(value), component);
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

template<typename ModelComponent>
bool PropertiesHelper::DrawModelComponent(ModelComponent& comp, ProjectManager* assetManager, const char* modelLabel)
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

	drawAssetPath(modelLabel, comp.model->GetAssetGUID());

	bool anyChanged = false;
	anyChanged |= DrawPropertyField<RenderStaticModel>("Visible", &comp.visible);

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
	auto& meshInfos = comp.model->GetMeshInfos();
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

	decltype(comp.model) instanceModel;
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
				instanceModel = comp.model->Clone(false, false);
			auto& editedMeshInfos = instanceModel->GetMeshInfos();
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
			instanceModel = comp.model->Clone(false, false);
		auto& editedMeshInfos = instanceModel->GetMeshInfos();
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
inline bool PropertiesHelper::DrawComponent<RenderStaticModel>(RenderStaticModel& comp, ProjectManager* assetManager)
{
	return DrawModelComponent(comp, assetManager, "StaticModel");
}

template<>
inline bool PropertiesHelper::DrawComponent<RenderSkinnedModel>(RenderSkinnedModel& comp, ProjectManager* assetManager)
{
	return DrawModelComponent(comp, assetManager, "SkinnedModel");
}

template<>
inline bool PropertiesHelper::DrawComponent<SkeletonAnimatorGroup>(SkeletonAnimatorGroup& comp, ProjectManager* assetManager)
{
	bool anyChanged = false;
	auto database = assetManager ? assetManager->GetDatabase() : nullptr;

	const auto assetLabel = [database](const AssetGUID& guid, const char* emptyLabel)
		{
			if (guid.empty())
				return std::string(emptyLabel);
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			return meta ? meta->name : guid;
		};
	const auto acceptDrop = [database](AssetType type, AssetGUID& target)
		{
			if (!ImGui::BeginDragDropTarget())
				return false;
			bool accepted = false;
			if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const auto* guidData = static_cast<const char*>(payload->Data);
				auto dropped = (guidData && guidData[0] != 0 && database) ? database->GetAssetMetaByGuid(AssetGUID(guidData)) : nullptr;
				if (dropped && dropped->type == type)
				{
					target = dropped->guid;
					accepted = true;
				}
			}
			ImGui::EndDragDropTarget();
			return accepted;
		};

	// 骨架是独立资产，这里只存 GUID；拖一个 Skeleton 进来。
	ImGui::TextUnformatted("Skeleton");
	ImGui::SameLine();
	ImGui::Button(assetLabel(comp.skeletonGuid, "No skeleton assigned").c_str(), ImVec2(220.f, 0.f));
	if (acceptDrop(AssetType::Skeleton, comp.skeletonGuid))
	{
		// 换了骨架，所有 animator 都得按新骨架重建，palette 长度才对得上。
		auto asset = assetManager ? assetManager->LoadAsset(comp.skeletonGuid) : nullptr;
		comp.SetSkeleton(asset ? asset->AsSkeleton() : nullptr, comp.skeletonGuid);
		anyChanged = true;
	}

	// 名字是 map 的键，编辑期间只改缓冲区，回车或失焦后才落到容器里，避免遍历时改 map。
	static std::unordered_map<std::string, std::string> nameEdits;
	std::string removeName;
	std::string renameFrom;
	std::string renameTo;
	std::string clipDropName;

	for (auto& [name, data] : comp.animatorDatas)
	{
		if (!data)
			continue;

		ImGui::PushID(name.c_str());

		ImGui::BeginChild("##animator_block", ImVec2(0.f, 104.f), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);

		bool enabled = data->enable;
		if (ImGui::Checkbox("##enable", &enabled))
		{
			data->enable = enabled;
			anyChanged = true;
		}

		ImGui::SameLine();
		auto editIt = nameEdits.find(name);
		if (editIt == nameEdits.end())
			editIt = nameEdits.emplace(name, name).first;

		char nameBuffer[64] = {};
		std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", editIt->second.c_str());

		// 空名字或重名先标红，回车或失焦时才真正改。
		const bool nameInvalid = editIt->second.empty() ||
			(editIt->second != name && comp.ExistAnimator(editIt->second));
		if (nameInvalid)
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.15f, 0.15f, 1.f));

		ImGui::SetNextItemWidth(160.f);
		const bool nameSubmitted =
			ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
			ImGui::IsItemDeactivatedAfterEdit();

		if (nameInvalid)
			ImGui::PopStyleColor();

		editIt->second = nameBuffer;
		if (nameSubmitted)
		{
			renameFrom = name;
			renameTo = editIt->second;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("Remove"))
			removeName = name;

		// Clip：拖动画资产进来，GUID 先记下，资产在遍历之后统一加载。
		ImGui::TextUnformatted("Clip");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.f);
		ImGui::Button(assetLabel(data->clipGuid, "No clip assigned").c_str(), ImVec2(-1.f, 0.f));
		if (acceptDrop(AssetType::AnimationClip, data->clipGuid))
			clipDropName = name;

		// Loop + 播放进度：进度直接问 animator，不要另存一份时间。
		ImGui::TextUnformatted("Loop");
		ImGui::SameLine();
		int loopCount = data->maxLoopCount;
		ImGui::SetNextItemWidth(90.f);
		if (ImGui::DragInt("##loop", &loopCount, 0.1f, 0, 99))
		{
			data->maxLoopCount = loopCount;
			anyChanged = true;
		}

		ImGui::SameLine();
		if (data->animator)
		{
			const float duration = data->animator->GetDurationTime();
			const float current = data->animator->GetCurrentTime();
			const float progress = duration > 0.f ? std::clamp(current / duration, 0.f, 1.f) : 0.f;
			char overlay[64] = {};
			std::snprintf(overlay, sizeof(overlay), "%.2f / %.2fs", current, duration);
			ImGui::ProgressBar(progress, ImVec2(-1.f, 0.f), overlay);
		}
		else
			ImGui::TextDisabled("no animator yet (need skeleton + clip)");

		ImGui::EndChild();
		ImGui::PopID();
	}

	// 拖进来的 clip：按 GUID 去工程里取资产，取不到就当没拖。
	if (!clipDropName.empty())
	{
		auto it = comp.animatorDatas.find(clipDropName);
		if (it != comp.animatorDatas.end() && it->second)
		{
			auto asset = assetManager ? assetManager->LoadAsset(it->second->clipGuid) : nullptr;
			comp.SetClip(clipDropName, asset ? asset->AsAnimationClip() : nullptr, it->second->clipGuid);
			anyChanged = true;
		}
	}

	if (!renameFrom.empty() && renameTo != renameFrom)
	{
		if (comp.RenameAnimator(renameFrom, renameTo))
		{
			nameEdits.erase(renameFrom);
			anyChanged = true;
		}
		else
		{
			nameEdits[renameFrom] = renameFrom;
			ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "Name must be non-empty and unique.");
		}
	}

	if (ImGui::Button("Add Animation"))
	{
		for (int index = 1; ; ++index)
		{
			std::string candidate = std::format("animator_{}", index);
			if (comp.ExistAnimator(candidate))
				continue;
			comp.AddAnimatorSlot(candidate, AssetGUID());
			anyChanged = true;
			break;
		}
	}

	if (!removeName.empty())
	{
		comp.RemoveAnimator(removeName);
		anyChanged = true;
	}

	ImGui::TextUnformatted("Default");
	ImGui::SameLine();
	const char* preview = comp.defaultAnimator.empty() ? "(none)" : comp.defaultAnimator.c_str();
	if (ImGui::BeginCombo("##default_animator", preview))
	{
		if (ImGui::Selectable("(none)", comp.defaultAnimator.empty()))
		{
			comp.defaultAnimator.clear();
			anyChanged = true;
		}
		for (auto& [name, data] : comp.animatorDatas)
		{
			if (ImGui::Selectable(name.c_str(), comp.defaultAnimator == name))
			{
				comp.defaultAnimator = name;
				anyChanged = true;
			}
		}
		ImGui::EndCombo();
	}

	ImGui::TextUnformatted("Idle");
	ImGui::SameLine();
	const char* idlePreview = comp.idleAnimator.empty() ? "(none)" : comp.idleAnimator.c_str();
	if (ImGui::BeginCombo("##idle_animator", idlePreview))
	{
		if (ImGui::Selectable("(none)", comp.idleAnimator.empty()))
		{
			comp.idleAnimator.clear();
			anyChanged = true;
		}
		for (auto& [name, data] : comp.animatorDatas)
		{
			if (ImGui::Selectable(name.c_str(), comp.idleAnimator == name))
			{
				comp.idleAnimator = name;
				anyChanged = true;
			}
		}
		ImGui::EndCombo();
	}

	return anyChanged;
}

// 粒子发射器：生成器/更新器是多态槽位，只能手写抽屉——先选种类，再画这个种类的字段。
template<>
inline bool PropertiesHelper::DrawComponent<ParticleEmitter>(ParticleEmitter& comp, ProjectManager* assetManager)
{
	bool changed = false;
	auto& p = comp.emitterParams;
	auto database = assetManager ? assetManager->GetDatabase() : nullptr;

	const auto assetLabel = [database](const AssetGUID& guid, const char* emptyLabel)
		{
			if (guid.empty())
				return std::string(emptyLabel);
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			return meta ? meta->name : guid;
		};
	const auto acceptDrop = [database](AssetType type, AssetGUID& target)
		{
			if (!ImGui::BeginDragDropTarget())
				return false;
			bool accepted = false;
			if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const auto* guidData = static_cast<const char*>(payload->Data);
				auto dropped = (guidData && guidData[0] != 0 && database) ? database->GetAssetMetaByGuid(AssetGUID(guidData)) : nullptr;
				if (dropped && dropped->type == type)
				{
					target = dropped->guid;
					accepted = true;
				}
			}
			ImGui::EndDragDropTarget();
			return accepted;
		};
	// 返回 -1 = 没选；否则是选中的种类下标。
	const auto kindCombo = [](const char* id, const char* const* names, int count, int current)
		{
			int picked = -1;
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo(id, current >= 0 ? names[current] : "(unset)"))
			{
				for (int index = 0; index < count; ++index)
					if (ImGui::Selectable(names[index], index == current))
						picked = index;
				ImGui::EndCombo();
			}
			return picked;
		};

	if (ImGui::Checkbox("Enable", &comp.enable))
		changed = true;

	ImGui::TextDisabled("Emission");
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Emit Rate", &p.emitRate, 0.1f, 0.f, 10000.f);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Rate Jitter", &p.emitRateJitter, 0.005f, 0.f, 1.f);

	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragInt("Burst Amount", &p.burstAmount, 0.2f, 0, 100000);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragInt("Burst Count", &p.burstCount, 0.2f, 0, 100000);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Burst Interval", &p.burstInterval, 0.01f, 0.f, 1000.f);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Warmup Time", &p.warmupTime, 0.01f, 0.f, 1000.f);

	ImGui::TextDisabled("Appearance");
	{
		static const char* rendererNames[] = { "Billboard", "Mesh" };
		const int current = std::holds_alternative<ParticleMeshRenderer>(p.renderer) ? 1 : 0;
		ImGui::SetNextItemWidth(150.f);
		if (ImGui::BeginCombo("##particle_renderer", rendererNames[current]))
		{
			for (int index = 0; index < 2; ++index)
			{
				if (ImGui::Selectable(rendererNames[index], index == current))
				{
					if (index == 0 && !std::holds_alternative<ParticleBillboardRenderer>(p.renderer))
						p.renderer = ParticleBillboardRenderer{};
					else if (index == 1 && !std::holds_alternative<ParticleMeshRenderer>(p.renderer))
						p.renderer = ParticleMeshRenderer{};
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Renderer");
	}

	if (auto* billboardRenderer = std::get_if<ParticleBillboardRenderer>(&p.renderer))
	{
		{
			static const char* facingNames[] = { "Camera Plane", "Camera Position", "Velocity Aligned", "Axis Locked" };
			const int current = static_cast<int>(billboardRenderer->facing);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_facing", facingNames[current]))
			{
				for (int index = 0; index < 4; ++index)
				{
					if (ImGui::Selectable(facingNames[index], index == current))
					{
						billboardRenderer->facing = static_cast<ParticleFacing>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Facing");
		}

		{
			static const char* maskNames[] = { "None", "Disc", "Soft Disc" };
			const int current = static_cast<int>(billboardRenderer->mask);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_mask", maskNames[current]))
			{
				for (int index = 0; index < 3; ++index)
				{
					if (ImGui::Selectable(maskNames[index], index == current))
					{
						billboardRenderer->mask = static_cast<ParticleMask>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Mask");
		}

		ImGui::TextUnformatted("Material");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.f);
		ImGui::Button(assetLabel(billboardRenderer->materialGuid, "None (drag a material here)").c_str(), ImVec2(-1.f, 0.f));
		if (acceptDrop(AssetType::Material, billboardRenderer->materialGuid))
		{
			auto asset = assetManager ? assetManager->LoadAsset(billboardRenderer->materialGuid) : nullptr;
			billboardRenderer->material = asset ? asset->AsMaterial() : nullptr;
			changed = true;
		}
		if (!billboardRenderer->materialGuid.empty())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("X##clear_material"))
			{
				billboardRenderer->materialGuid = AssetGUID();
				billboardRenderer->material.reset();
				changed = true;
			}
		}
	}
	else if (auto* meshRenderer = std::get_if<ParticleMeshRenderer>(&p.renderer))
	{
		// 网格类型：Cube / Sphere 用内置网格，Custom 用用户自己的模型
		{
			static const char* meshTypeNames[] = { "Cube", "Sphere", "Custom" };
			const int current = static_cast<int>(meshRenderer->meshType);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_mesh_type", meshTypeNames[current]))
			{
				for (int index = 0; index < 3; ++index)
				{
					if (ImGui::Selectable(meshTypeNames[index], index == current))
					{
						meshRenderer->meshType = static_cast<ParticleMeshType>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Mesh Type");
		}

		if (meshRenderer->meshType == ParticleMeshType::Custom)
		{
			// Custom：几何与材质都来自模型本身
			ImGui::TextUnformatted("Model");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.f);
			ImGui::Button(assetLabel(meshRenderer->modelGuid, "None (drag a model here)").c_str(), ImVec2(-1.f, 0.f));
			if (acceptDrop(AssetType::StaticModel, meshRenderer->modelGuid))
			{
				auto asset = assetManager ? assetManager->LoadAsset(meshRenderer->modelGuid) : nullptr;
				meshRenderer->model = asset ? asset->AsStaticModel() : nullptr;
				changed = true;
			}
			if (!meshRenderer->modelGuid.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("X##clear_model"))
				{
					meshRenderer->modelGuid = AssetGUID();
					meshRenderer->model.reset();
					changed = true;
				}
			}
		}
		else
		{
			// Cube / Sphere：内置网格，材质可选，留空就只用粒子的 tint 颜色
			ImGui::TextUnformatted("Material");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.f);
			ImGui::Button(assetLabel(meshRenderer->materialGuid, "None (particle tint only)").c_str(), ImVec2(-1.f, 0.f));
			if (acceptDrop(AssetType::Material, meshRenderer->materialGuid))
			{
				auto asset = assetManager ? assetManager->LoadAsset(meshRenderer->materialGuid) : nullptr;
				meshRenderer->material = asset ? asset->AsMaterial() : nullptr;
				changed = true;
			}
			if (!meshRenderer->materialGuid.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("X##clear_mesh_material"))
				{
					meshRenderer->materialGuid = AssetGUID();
					meshRenderer->material.reset();
					changed = true;
				}
			}
		}
	}

	// 混合模式：加色顺序无关、直出；AlphaBlend 走 OIT 的权重累积，两者可以逐发射器选。
	{
		static const char* blendNames[] = { "Additive", "Alpha Blend (OIT)" };
		const int current = static_cast<int>(p.blendMode);
		ImGui::SetNextItemWidth(150.f);
		if (ImGui::BeginCombo("##particle_blend_mode", blendNames[current]))
		{
			for (int index = 0; index < 2; ++index)
			{
				if (ImGui::Selectable(blendNames[index], index == current))
				{
					p.blendMode = static_cast<ParticleBlendMode>(index);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Blend");
	}

	// ------------------------------------------------------------ 生成器
	ImGui::TextDisabled("Generators");

	// scaleGenerator
	if (ImGui::TreeNode("Scale Generator"))
	{
		auto* random = dynamic_cast<RandomScaleGenerator*>(p.scaleGenerator.get());
		auto* fixed = dynamic_cast<FixedScaleGenerator*>(p.scaleGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##scale_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(0.05f), glm::vec3(0.15f));
			else
				p.scaleGenerator = std::make_shared<FixedScaleGenerator>(glm::vec3(0.1f));
			random = dynamic_cast<RandomScaleGenerator*>(p.scaleGenerator.get());
			fixed = dynamic_cast<FixedScaleGenerator*>(p.scaleGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_min", &random->min.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("min");
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_max", &random->max.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("max");
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_value", &fixed->scale.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("scale");
		}
		ImGui::TreePop();
	}

	// colorGenerator
	if (ImGui::TreeNode("Color Generator"))
	{
		auto* random = dynamic_cast<RandomColorGenerator*>(p.colorGenerator.get());
		auto* fixed = dynamic_cast<FixedColorGenerator*>(p.colorGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##color_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(0.2f), glm::vec3(1.0f));
			else
				p.colorGenerator = std::make_shared<FixedColorGenerator>(glm::vec3(1.0f));
			random = dynamic_cast<RandomColorGenerator*>(p.colorGenerator.get());
			fixed = dynamic_cast<FixedColorGenerator*>(p.colorGenerator.get());
			changed = true;
		}
		if (random)
		{
			changed |= ImGui::ColorEdit3("min##color_min", &random->min.x);
			changed |= ImGui::ColorEdit3("max##color_max", &random->max.x);
		}
		else if (fixed)
			changed |= ImGui::ColorEdit3("value##color_value", &fixed->color.x);
		ImGui::TreePop();
	}

	// lifeTimeGenerator
	if (ImGui::TreeNode("Lifetime Generator"))
	{
		auto* random = dynamic_cast<RandomLifeTimeGenerator*>(p.lifeTimeGenerator.get());
		auto* fixed = dynamic_cast<FixedLifeTimeGenerator*>(p.lifeTimeGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##life_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(0.8f, 1.6f);
			else
				p.lifeTimeGenerator = std::make_shared<FixedLifeTimeGenerator>(2.0f);
			random = dynamic_cast<RandomLifeTimeGenerator*>(p.lifeTimeGenerator.get());
			fixed = dynamic_cast<FixedLifeTimeGenerator*>(p.lifeTimeGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("min##life_min", &random->min, 0.01f, 0.01f, 1000.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("max##life_max", &random->max, 0.01f, 0.01f, 1000.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("lifetime##life_value", &fixed->lifetime, 0.01f, 0.01f, 1000.f);
		}
		ImGui::TreePop();
	}

	// opacityGenerator
	if (ImGui::TreeNode("Opacity Generator"))
	{
		auto* random = dynamic_cast<RandomOpacityGenerator*>(p.opacityGenerator.get());
		auto* fixed = dynamic_cast<FixedOpactiyGenerator*>(p.opacityGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##opacity_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.opacityGenerator = std::make_shared<RandomOpacityGenerator>(0.5f, 1.0f);
			else
				p.opacityGenerator = std::make_shared<FixedOpactiyGenerator>(1.0f);
			random = dynamic_cast<RandomOpacityGenerator*>(p.opacityGenerator.get());
			fixed = dynamic_cast<FixedOpactiyGenerator*>(p.opacityGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("min##opacity_min", &random->min, 0.f, 1.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("max##opacity_max", &random->max, 0.f, 1.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("opacity##opacity_value", &fixed->opacity, 0.f, 1.f);
		}
		ImGui::TreePop();
	}

	// positionGenerator
	if (ImGui::TreeNode("Position Generator"))
	{
		auto* spherical = dynamic_cast<SphericalGenerator*>(p.posGenerator.get());
		auto* ball = dynamic_cast<BallGenerator*>(p.posGenerator.get());
		auto* box = dynamic_cast<BoxGenerator*>(p.posGenerator.get());
		const bool point = p.posGenerator && !spherical && !ball && !box;
		static const char* kinds[] = { "Point", "Spherical", "Ball", "Box" };
		const int current = point ? 0 : (spherical ? 1 : (ball ? 2 : (box ? 3 : -1)));
		const int picked = kindCombo("##pos_kind", kinds, 4, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.posGenerator = std::make_shared<PointGenerator>(); break;
			case 1: p.posGenerator = std::make_shared<SphericalGenerator>(1.0f); break;
			case 2: p.posGenerator = std::make_shared<BallGenerator>(1.0f); break;
			default: p.posGenerator = std::make_shared<BoxGenerator>(glm::vec3(1.0f)); break;
			}
			spherical = dynamic_cast<SphericalGenerator*>(p.posGenerator.get());
			ball = dynamic_cast<BallGenerator*>(p.posGenerator.get());
			box = dynamic_cast<BoxGenerator*>(p.posGenerator.get());
			changed = true;
		}
		if (spherical || ball)
		{
			float* radius = spherical ? &spherical->radius : &ball->radius;
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("radius##pos_radius", radius, 0.01f, 0.f, 1000.f);
		}
		else if (box)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("half size##pos_half", &box->halfSize.x, 0.01f, 0.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// directionGenerator
	if (ImGui::TreeNode("Direction Generator"))
	{
		auto* fixedCone = dynamic_cast<FixedDirectionConeGenerator*>(p.dirGenerator.get());
		auto* follow = dynamic_cast<FollowEmitterDirectionConeGenerator*>(p.dirGenerator.get());
		auto* radial = dynamic_cast<RadialDirectionGenerator*>(p.dirGenerator.get());
		static const char* kinds[] = { "Fixed Cone", "Emitter Cone", "Radial" };
		const int current = fixedCone ? 0 : (follow ? 1 : (radial ? 2 : -1));
		const int picked = kindCombo("##dir_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.dirGenerator = std::make_shared<FixedDirectionConeGenerator>(glm::vec3(0.f, 0.f, -1.f), 0.2f); break;
			case 1: p.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.3f); break;
			default: p.dirGenerator = std::make_shared<RadialDirectionGenerator>(0.3f); break;
			}
			fixedCone = dynamic_cast<FixedDirectionConeGenerator*>(p.dirGenerator.get());
			follow = dynamic_cast<FollowEmitterDirectionConeGenerator*>(p.dirGenerator.get());
			radial = dynamic_cast<RadialDirectionGenerator*>(p.dirGenerator.get());
			changed = true;
		}
		if (fixedCone)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("direction##dir_value", &fixedCone->direction.x, 0.01f, -1.f, 1.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread", &fixedCone->spreadAngle, 0.f, 3.14159f);
		}
		else if (follow)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread_follow", &follow->spreadAngle, 0.f, 3.14159f);
		}
		else if (radial)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread_radial", &radial->spreadAngle, 0.f, 3.14159f);
		}
		ImGui::TreePop();
	}

	// rotationGenerator
	if (ImGui::TreeNode("Rotation Generator"))
	{
		auto* fixedRotation = dynamic_cast<FixedRotationGenerator*>(p.rotGenerator.get());
		auto* followEmitter = dynamic_cast<FollowEmitterRotationGenerator*>(p.rotGenerator.get());
		auto* followVelocity = dynamic_cast<FollowVelocityRotationGenerator*>(p.rotGenerator.get());
		static const char* kinds[] = { "Fixed", "Emitter", "Velocity" };
		const int current = fixedRotation ? 0 : (followEmitter ? 1 : (followVelocity ? 2 : -1));
		const int picked = kindCombo("##rot_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.rotGenerator = std::make_shared<FixedRotationGenerator>(glm::identity<glm::quat>()); break;
			case 1: p.rotGenerator = std::make_shared<FollowEmitterRotationGenerator>(); break;
			default: p.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>(); break;
			}
			fixedRotation = dynamic_cast<FixedRotationGenerator*>(p.rotGenerator.get());
			changed = true;
		}
		if (fixedRotation)
		{
			float quat[4] = { fixedRotation->rotation.w, fixedRotation->rotation.x, fixedRotation->rotation.y, fixedRotation->rotation.z };
			ImGui::SetNextItemWidth(220.f);
			if (ImGui::DragFloat4("quat##rot_value", quat, 0.01f, -1.f, 1.f))
			{
				fixedRotation->rotation = glm::normalize(glm::quat(quat[0], quat[1], quat[2], quat[3]));
				changed = true;
			}
		}
		ImGui::TreePop();
	}

	// speedGenerator
	if (ImGui::TreeNode("Speed Generator"))
	{
		auto* random = dynamic_cast<RandomSpeedGenerator*>(p.speedGenerator.get());
		auto* fixed = dynamic_cast<FixedSpeedGenerator*>(p.speedGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##speed_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.speedGenerator = std::make_shared<RandomSpeedGenerator>(0.5f, 1.5f);
			else
				p.speedGenerator = std::make_shared<FixedSpeedGenerator>(1.0f);
			random = dynamic_cast<RandomSpeedGenerator*>(p.speedGenerator.get());
			fixed = dynamic_cast<FixedSpeedGenerator*>(p.speedGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("min##speed_min", &random->min, 0.01f, -1000.f, 1000.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("max##speed_max", &random->max, 0.01f, -1000.f, 1000.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("speed##speed_value", &fixed->speed, 0.01f, -1000.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// positionOffsetGenerator
	if (ImGui::TreeNode("Position Offset Generator"))
	{
		auto* fixed = dynamic_cast<FixedPositionOffsetGenerator*>(p.posOffsetGenerator.get());
		auto* radial = dynamic_cast<RadialPositionOffsetGenerator*>(p.posOffsetGenerator.get());
		static const char* kinds[] = { "Fixed", "Radial" };
		const int current = fixed ? 0 : (radial ? 1 : -1);
		const int picked = kindCombo("##offset_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.posOffsetGenerator = std::make_shared<FixedPositionOffsetGenerator>(glm::vec3(0.0f));
			else
				p.posOffsetGenerator = std::make_shared<RadialPositionOffsetGenerator>();
			fixed = dynamic_cast<FixedPositionOffsetGenerator*>(p.posOffsetGenerator.get());
			changed = true;
		}
		if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("offset##offset_value", &fixed->offset.x, 0.01f, -1000.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// ------------------------------------------------------------ 更新器
	ImGui::TextDisabled("Updaters");

	// opacityUpdater
	if (ImGui::TreeNode("Opacity Updater"))
	{
		auto* linear = dynamic_cast<LinearOpacityUpdater*>(p.opacityUpdater.get());
		static const char* kinds[] = { "None", "Linear" };
		const int current = linear ? 1 : 0;
		const int picked = kindCombo("##opacityupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.opacityUpdater.reset();
			else
				p.opacityUpdater = std::make_shared<LinearOpacityUpdater>(0.0f);
			linear = dynamic_cast<LinearOpacityUpdater*>(p.opacityUpdater.get());
			changed = true;
		}
		if (linear)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("goal##opacityupd_goal", &linear->goalOpacity, 0.f, 1.f);
		}
		ImGui::TreePop();
	}

	// baseColorUpdater
	if (ImGui::TreeNode("Base Color Updater"))
	{
		auto* linear = dynamic_cast<LinearBaseColorUpdater*>(p.baseColorUpdater.get());
		auto* fire = dynamic_cast<FireColorUpdater*>(p.baseColorUpdater.get());
		static const char* kinds[] = { "None", "Linear", "Fire" };
		const int current = linear ? 1 : (fire ? 2 : 0);
		const int picked = kindCombo("##colorupd_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.baseColorUpdater.reset();
			else if (picked == 1)
				p.baseColorUpdater = std::make_shared<LinearBaseColorUpdater>(glm::vec3(1.0f));
			else
				p.baseColorUpdater = std::make_shared<FireColorUpdater>();
			linear = dynamic_cast<LinearBaseColorUpdater*>(p.baseColorUpdater.get());
			fire = dynamic_cast<FireColorUpdater*>(p.baseColorUpdater.get());
			changed = true;
		}
		if (linear)
			changed |= ImGui::ColorEdit3("goal##colorupd_goal", &linear->goalColor.x);
		else if (fire)
		{
			changed |= ImGui::ColorEdit3("inner##fire_inner", &fire->innerColor.x);
			changed |= ImGui::ColorEdit3("mid##fire_mid", &fire->midColor.x);
			changed |= ImGui::ColorEdit3("outer##fire_outer", &fire->outerColor.x);
		}
		ImGui::TreePop();
	}

	// scaleUpdater
	if (ImGui::TreeNode("Scale Updater"))
	{
		auto* linear = dynamic_cast<LinearScaleUpdater*>(p.scaleUpdater.get());
		static const char* kinds[] = { "None", "Linear" };
		const int current = linear ? 1 : 0;
		const int picked = kindCombo("##scaleupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.scaleUpdater.reset();
			else
				p.scaleUpdater = std::make_shared<LinearScaleUpdater>(glm::vec3(0.1f));
			linear = dynamic_cast<LinearScaleUpdater*>(p.scaleUpdater.get());
			changed = true;
		}
		if (linear)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("goal##scaleupd_goal", &linear->goalScale.x, 0.001f, 0.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// rotationUpdater
	if (ImGui::TreeNode("Rotation Updater"))
	{
		auto* follow = dynamic_cast<FollowVelocityRotationUpdater*>(p.rotationUpdater.get());
		static const char* kinds[] = { "None", "Follow Velocity" };
		const int current = follow ? 1 : 0;
		const int picked = kindCombo("##rotupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.rotationUpdater.reset();
			else
				p.rotationUpdater = std::make_shared<FollowVelocityRotationUpdater>();
			changed = true;
		}
		ImGui::TreePop();
	}

	// 局部/世界加速度：同一套 5 种，None = 空指针。
	const auto drawAccelerationUpdater = [&](const char* label, std::shared_ptr<IAccelerationUpdater>& slot)
		{
			auto* gravity = dynamic_cast<GravityAccelerationUpdater*>(slot.get());
			auto* linear = dynamic_cast<LinearAccelerationUpdater*>(slot.get());
			auto* perlin = dynamic_cast<PerlinTurbulenceUpdater*>(slot.get());
			auto* fixed = (gravity || linear || perlin) ? nullptr : dynamic_cast<FiexedAccelerationUpdater*>(slot.get());
			static const char* kinds[] = { "None", "Fixed", "Gravity", "Linear", "Perlin" };
			const int current = slot == nullptr ? 0 : (fixed ? 1 : (gravity ? 2 : (linear ? 3 : (perlin ? 4 : -1))));
			const int picked = kindCombo(label, kinds, 5, current);
			bool localChanged = false;
			if (picked >= 0 && picked != current)
			{
				switch (picked)
				{
				case 0: slot.reset(); break;
				case 1: slot = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(0.f, -9.8f, 0.f)); break;
				case 2: slot = std::make_shared<GravityAccelerationUpdater>(); break;
				case 3: slot = std::make_shared<LinearAccelerationUpdater>(glm::vec3(0.f, 1.f, 0.f), 0.f, 9.8f); break;
				default: slot = std::make_shared<PerlinTurbulenceUpdater>(0.5f, 1.5f); break;
				}
				gravity = dynamic_cast<GravityAccelerationUpdater*>(slot.get());
				linear = dynamic_cast<LinearAccelerationUpdater*>(slot.get());
				perlin = dynamic_cast<PerlinTurbulenceUpdater*>(slot.get());
				fixed = (gravity || linear || perlin) ? nullptr : dynamic_cast<FiexedAccelerationUpdater*>(slot.get());
				localChanged = true;
			}
			if (fixed && !gravity)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat3("acceleration##acc_value", &fixed->acceleration.x, 0.01f, -1000.f, 1000.f);
			}
			else if (gravity)
				ImGui::TextDisabled("acceleration = %s", std::format("({:.2f}, {:.2f}, {:.2f})", gravity->acceleration.x, gravity->acceleration.y, gravity->acceleration.z).c_str());
			else if (linear)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat3("dir##acc_dir", &linear->accelerationDir.x, 0.01f, -1.f, 1.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("init##acc_init", &linear->initAccelerationValue, 0.01f, -1000.f, 1000.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("goal##acc_goal", &linear->goalAccelerationValue, 0.01f, -1000.f, 1000.f);
			}
			else if (perlin)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("strength##perlin_strength", &perlin->strength, 0.01f, 0.f, 100.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("frequency##perlin_freq", &perlin->frequency, 0.01f, 0.f, 100.f);
			}
			return localChanged;
		};

	// localAccelerationUpdater
	if (ImGui::TreeNode("Local Acceleration Updater"))
	{
		changed |= drawAccelerationUpdater("##local_acc_kind", p.localAccelerationUpdater);
		ImGui::TreePop();
	}

	// worldAccelerationUpdater
	if (ImGui::TreeNode("World Acceleration Updater"))
	{
		changed |= drawAccelerationUpdater("##world_acc_kind", p.worldAccelerationUpdater);
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::SameLine();
	if (!p.IsVaild())
		ImGui::TextColored(ImVec4(1.f, 0.55f, 0.35f, 1.f), "Generators incomplete - nothing will emit");

	return changed;
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
