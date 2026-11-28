
#include "PropertiesHelper.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "SceneCodec/ComponentCodec.h"
#include "DDGIEditor.h"
#include "EditorTags.h"
#include "SceneEditor.h"
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <format>

namespace
{
    NComponentCodec::ComponentCodec* FindCodec(ComponentTypeID typeID)
    {
        auto& codecs = NComponentCodec::GetComponentCodecs();
        auto it = std::find_if(codecs.begin(), codecs.end(), [typeID](const auto& codec) {
            return codec.componentType == typeID;
            });
        return it == codecs.end() ? nullptr : &*it;
    }

    bool DrawModuleProperty(Entity entity, ComponentTypeID componentType, const NComponentCodec::ComponentProperty& property)
    {
        if (!property.read)
            return false;

        auto value = property.read(entity);
        if (value.is_discarded() || value.is_null())
        {
            ImGui::TextDisabled("%s: <unavailable>", property.displayName.c_str());
            return false;
        }

        ImGui::PushID(property.fieldId.c_str());
        bool changed = false;
        const char* label = property.displayName.c_str();
        if (property.readOnly)
        {
            ImGui::Text("%s: %s", label, value.dump().c_str());
            ImGui::PopID();
            return false;
        }

        switch (property.kind)
        {
        case NComponentCodec::EditorPropertyKind::Boolean:
        {
            bool v = value.get<bool>();
            changed = ImGui::Checkbox(label, &v);
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::Integer:
        {
            int v = value.get<int>();
            changed = ImGui::DragInt(label, &v, static_cast<float>(property.speed),
                static_cast<int>(property.minValue), static_cast<int>(property.maxValue));
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::UnsignedInteger:
        {
            uint32_t v = value.get<uint32_t>();
            uint32_t minV = static_cast<uint32_t>(std::max(0.0, property.minValue));
            uint32_t maxV = static_cast<uint32_t>(std::max(0.0, property.maxValue));
            changed = ImGui::DragScalar(label, ImGuiDataType_U32, &v, static_cast<float>(property.speed),
                property.maxValue > 0 ? &minV : nullptr,
                property.maxValue > 0 ? &maxV : nullptr);
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::Integer64:
        {
            int64_t v = value.get<int64_t>();
            changed = ImGui::DragScalar(label, ImGuiDataType_S64, &v, static_cast<float>(property.speed));
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::Float:
        {
            float v = value.get<float>();
            changed = ImGui::DragFloat(label, &v, static_cast<float>(property.speed),
                static_cast<float>(property.minValue), static_cast<float>(property.maxValue));
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::Double:
        {
            double v = value.get<double>();
            changed = ImGui::DragScalar(label, ImGuiDataType_Double, &v, static_cast<float>(property.speed));
            if (changed) value = v;
            break;
        }
        case NComponentCodec::EditorPropertyKind::String:
        {
            std::string v = value.get<std::string>();
            char buffer[512]{};
            strncpy_s(buffer, v.c_str(), _TRUNCATE);
            if (ImGui::InputText(label, buffer, sizeof(buffer))) { value = buffer; changed = true; }
            break;
        }
        case NComponentCodec::EditorPropertyKind::Vec2:
        case NComponentCodec::EditorPropertyKind::Vec3:
        case NComponentCodec::EditorPropertyKind::Vec4:
        {
            float v[4]{};
            const int count = property.kind == NComponentCodec::EditorPropertyKind::Vec2 ? 2
                : property.kind == NComponentCodec::EditorPropertyKind::Vec3 ? 3 : 4;
            if (!value.is_array() || value.size() != count) break;
            for (int i = 0; i < count; ++i) v[i] = value[i].get<float>();
            changed = count == 2 ? ImGui::DragFloat2(label, v, static_cast<float>(property.speed))
                : count == 3 ? ImGui::DragFloat3(label, v, static_cast<float>(property.speed))
                : ImGui::DragFloat4(label, v, static_cast<float>(property.speed));
            if (changed) value = NComponentCodec::Json::array();
            if (changed) for (int i = 0; i < count; ++i) value.push_back(v[i]);
            break;
        }
        case NComponentCodec::EditorPropertyKind::Enumeration:
        {
            if (property.enumOptions.empty())
            {
                int v = value.get<int>();
                changed = ImGui::DragInt(label, &v, 1);
                if (changed) value = v;
                break;
            }
            int selected = -1;
            const auto current = value.get<int64_t>();
            for (int i = 0; i < static_cast<int>(property.enumOptions.size()); ++i)
                if (property.enumOptions[i].value == current) { selected = i; break; }
            const char* preview = selected >= 0 ? property.enumOptions[selected].label.c_str() : "<invalid>";
            if (ImGui::BeginCombo(label, preview))
            {
                for (int i = 0; i < static_cast<int>(property.enumOptions.size()); ++i)
                {
                    const bool isSelected = i == selected;
                    if (ImGui::Selectable(property.enumOptions[i].label.c_str(), isSelected))
                    {
                        value = property.enumOptions[i].value;
                        changed = true;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        }
        ImGui::PopID();
        if (changed && property.write)
        {
            if (auto* world = entity.getWorld())
            {
                const auto fieldId = property.fieldId;
                world->SubmitCommand([entity, componentType, fieldId, value = std::move(value)]() mutable {
                    auto* codec = FindCodec(componentType);
                    if (!codec) return;
                    auto it = std::find_if(codec->editorProperties.begin(), codec->editorProperties.end(),
                        [&](const auto& candidate) { return candidate.fieldId == fieldId; });
                    if (it != codec->editorProperties.end() && !it->readOnly && it->write)
                        it->write(entity, value);
                    });
            }
        }
        return changed;
    }
}

namespace
{
    // ---------------- 名称（TagEditorName） ----------------
    // 输入时每改一个字就提交（层级面板、视口图标的名字马上跟着变）；切换选中、点别处都不会丢掉正在输入的内容。
    // 全是空白时不提交（保留原来的名字），输入框失去焦点后显示回实际的名字。Esc 恢复成开始编辑前的名字
    struct NameEditState
    {
        EntityID entity = 0;
        bool valid = false;
        std::string buffer;
    };
    NameEditState s_nameEdit;

    int ResizeStringCallback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            auto* text = static_cast<std::string*>(data->UserData);
            text->resize(static_cast<size_t>(data->BufTextLen));
            data->Buf = text->data();
        }
        return 0;
    }

    std::string TrimName(const std::string& name)
    {
        const auto first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = name.find_last_not_of(" \t\r\n");
        return name.substr(first, last - first + 1);
    }

    void DrawEditorNameTag(Entity entity, IComponent* comp, ProjectManager*)
    {
        auto* tag = static_cast<TagEditorName*>(comp);
        if (!entity || !tag)
            return;

        const ImGuiID inputId = ImGui::GetID("##EditorName");
        const bool editing = ImGui::GetActiveID() == inputId;
        // 没在输入（或者换了实体）时，输入框显示实际的名字
        if (!editing || !s_nameEdit.valid || s_nameEdit.entity != entity.getId())
            s_nameEdit = { entity.getId(), true, tag->name };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        // capacity + 1：InputText 需要缓冲区里留出结尾的 '\0'
        s_nameEdit.buffer.reserve(s_nameEdit.buffer.size() + 1);
        const bool changed = ImGui::InputTextWithHint("##EditorName", "实体名称", s_nameEdit.buffer.data(), s_nameEdit.buffer.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AutoSelectAll, ResizeStringCallback, &s_nameEdit.buffer);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("编辑实体名称（层级面板里双击也可以重命名）。名称为空时不保存");

        const std::string name = TrimName(s_nameEdit.buffer);
        if (ImGui::IsItemActive() && name.empty())
            ImGui::TextDisabled("名称为空，不会保存");

        if (changed && !name.empty() && name != tag->name)
        {
            if (auto* world = entity.getWorld())
                world->SubmitCommand([entity, name](World& w)
                    {
                        if (w.IsValidWorldEntity(entity))
                            SetNameTag(entity, name);
                    });
        }
    }
}

// 在程序初始化时注册所有组件
static void RegisterAllComponents()
{
	PropertiesHelper::RegisterComponent<Transform>();
    PropertiesHelper::RegisterComponent<Physics>();
    PropertiesHelper::RegisterComponent<Movement>();
    PropertiesHelper::RegisterComponent<RenderLight>();
    PropertiesHelper::RegisterComponent<CameraComponent>();
    PropertiesHelper::RegisterComponent<TagFreeCamera>();
    PropertiesHelper::RegisterComponent<RenderStaticModel>();
    PropertiesHelper::RegisterComponent<RenderSkinnedModel>();
    PropertiesHelper::RegisterComponent<SkeletonAnimatorGroup>();
    PropertiesHelper::RegisterComponent<ParticleEmitter>();
    PropertiesHelper::RegisterComponent<Script>();
    PropertiesHelper::RegisterComponent<UIView>();
    PropertiesHelper::RegisterComponent<TagCurrentControl>();
    PropertiesHelper::RegisterComponent<CameraFollow>();
    PropertiesHelper::RegisterComponent<Transform2D>();
    PropertiesHelper::RegisterComponent<Sprite>();
    PropertiesHelper::RegisterComponent<AnimatedImagePlayer>();
    PropertiesHelper::RegisterComponent<Camera2D>();
    PropertiesHelper::RegisterCustomDraw(ComponentType<DDGIVolumeComponent>::getId(), &DDGIEditor::DrawVolumeComponent);
    // 名称：不依赖编解码器的注册顺序（RegisterComponent 要先找到 codec），直接注册绘制函数
    PropertiesHelper::RegisterCustomDraw(ComponentType<TagEditorName>::getId(), &DrawEditorNameTag);
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
    auto& registry = GetCustomDrawRegistry();

    for (const auto& codec : NComponentCodec::GetComponentCodecs())
    {
        const bool hasCustomDrawer = registry.contains(codec.componentType);
        if (!codec.editorBehavior.visible || (
            (!hasCustomDrawer && codec.moduleId.empty() && codec.editorProperties.empty())
			) || !codec.has || !codec.has(entity))
            continue;
		const std::string displayName = codec.displayName.empty() ? codec.type : codec.displayName;
		result.push_back({ codec.componentType, displayName, entity.tryGetComponentByTypeID(codec.componentType), codec.editorBehavior.removable });
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

		ImGui::PushID(static_cast<int>(entity.getId()));
		const std::string typeIdString = std::to_string(data.typeID);
		ImGui::PushID(typeIdString.c_str());
		bool expanded = false;
		if (ImGui::BeginTable("ComponentHeader", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_NoPadOuterX))
        {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 58.0f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			expanded = ImGui::CollapsingHeader(data.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::TableSetColumnIndex(1);
			if (!data.removable) ImGui::BeginDisabled();
			if (ImGui::SmallButton("Remove"))
				ImGui::OpenPopup("ConfirmComponentRemoval");
			if (!data.removable) ImGui::EndDisabled();

			if (ImGui::BeginPopup("ConfirmComponentRemoval"))
			{
				ImGui::Text("Remove %s from this entity?", data.name.c_str());
				if (ImGui::Button("Remove Component"))
				{
					const Entity target = entity;
					const ComponentTypeID typeID = data.typeID;
					if (auto* world = entity.getWorld())
						world->SubmitCommand([target, typeID]() {
							auto* targetWorld = target.getWorld();
							if (!targetWorld || !targetWorld->IsValidWorldEntity(target)) return;
							if (auto* codec = FindCodec(typeID); codec && codec->editorBehavior.remove)
								codec->editorBehavior.remove(target);
							else
								targetWorld->removeComponentByTypeID(target, typeID);
						});
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			ImGui::EndTable();
		}

		if (expanded)
		{
            auto& registry = GetCustomDrawRegistry();
            auto it = registry.find(data.typeID);
            if (it != registry.end())
            {
                it->second(entity, data.component, assetManager);
            }
            else if (auto* codec = FindCodec(data.typeID))
            {
                for (const auto& property : codec->editorProperties)
                    DrawModuleProperty(entity, data.typeID, property);
            }
        }
		ImGui::PopID();
		ImGui::PopID();
    }
}

std::unordered_map<ComponentTypeID, PropertiesHelper::DrawFunction>& PropertiesHelper::GetCustomDrawRegistry()
{
    static std::unordered_map<ComponentTypeID, PropertiesHelper::DrawFunction> registry;
    return registry;
}

void PropertiesHelper::SetNextPropertyFieldWidth(float maxWidth, float fraction)
{
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::min(maxWidth, availableWidth * fraction));
}
