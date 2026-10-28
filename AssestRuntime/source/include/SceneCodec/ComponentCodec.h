#pragma once

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <glm/glm.hpp>
#include "ECSCore/World.h"
#include "ECSCore/ComponentTypes.h"
#include <nlohmann/json.hpp>


namespace NComponentCodec
{
    using Json = nlohmann::json;

    struct ComponentData
    {
        std::string type;
        Json data = Json::object();
    };

    struct EntityData
    {
        std::string id;
        std::string name;
        std::string parentId;
        std::vector<ComponentData> components;
    };

    enum class EditorPropertyKind : uint32_t
    {
        Boolean,
        Integer,
        UnsignedInteger,
        Integer64,
        Float,
        Double,
        String,
        Vec2,
        Vec3,
        Vec4,
        Enumeration
    };

    struct EditorEnumOption
    {
        int64_t value = 0;
        std::string label;
    };

    struct ComponentProperty
    {
        std::string fieldId;
        std::string displayName;
        EditorPropertyKind kind = EditorPropertyKind::Float;
        bool readOnly = false;
        double minValue = 0.0;
        double maxValue = 0.0;
        double speed = 0.1;
        std::vector<EditorEnumOption> enumOptions;
        std::function<Json(Entity)> read;
        std::function<bool(Entity, const Json&)> write;
    };

    struct ComponentEditorBehavior
    {
        bool visible = true;
        bool addable = true;
        bool removable = true;
        std::function<void(Entity)> add;
        std::function<bool(Entity)> remove;
    };

    template<typename T>
    Json EncodeEditorProperty(const T& value)
    {
        if constexpr (std::is_enum_v<T>) return static_cast<std::underlying_type_t<T>>(value);
        else if constexpr (std::is_same_v<T, glm::vec2>) return Json{ value.x, value.y };
        else if constexpr (std::is_same_v<T, glm::vec3>) return Json{ value.x, value.y, value.z };
        else if constexpr (std::is_same_v<T, glm::vec4>) return Json{ value.x, value.y, value.z, value.w };
        else return value;
    }

    template<typename T>
    bool DecodeEditorProperty(const Json& value, T& out)
    {
        try
        {
            if constexpr (std::is_enum_v<T>)
            {
                if (!value.is_number_integer()) return false;
                out = static_cast<T>(value.get<std::underlying_type_t<T>>());
            }
            else if constexpr (std::is_same_v<T, glm::vec2>)
            {
                if (!value.is_array() || value.size() != 2) return false;
                out = glm::vec2(value[0].get<float>(), value[1].get<float>());
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                if (!value.is_array() || value.size() != 3) return false;
                out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
            }
            else if constexpr (std::is_same_v<T, glm::vec4>)
            {
                if (!value.is_array() || value.size() != 4) return false;
                out = glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
            }
            else out = value.get<T>();
            return true;
        }
        catch (const Json::exception&) { return false; }
    }

    template<typename Owner, typename Field>
    ComponentProperty MakeComponentProperty(
        std::string fieldId,
        std::string displayName,
        Field Owner::* member,
        EditorPropertyKind kind,
        bool readOnly = false,
        double minValue = 0.0,
        double maxValue = 0.0,
        double speed = 0.1,
        std::vector<EditorEnumOption> enumOptions = {})
    {
        ComponentProperty property;
        property.fieldId = std::move(fieldId);
        property.displayName = std::move(displayName);
        property.kind = kind;
        property.readOnly = readOnly;
        property.minValue = minValue;
        property.maxValue = maxValue;
        property.speed = speed;
        property.enumOptions = std::move(enumOptions);
        property.read = [member](Entity entity) {
            auto* component = entity.tryGetComponent<Owner>();
            return component ? EncodeEditorProperty(component->*member) : Json{};
            };
        property.write = [member](Entity entity, const Json& value) {
            auto* component = entity.tryGetComponent<Owner>();
            if (!component) return false;
            Field decoded{};
            if (!DecodeEditorProperty(value, decoded)) return false;
            component->*member = std::move(decoded);
            return true;
            };
        return property;
    }

    struct ComponentCodec
    {
        std::string type;
        std::string displayName;
        uint32_t version = 1;
        std::string moduleId;
        ComponentTypeID componentType = 0;
        std::string nativeTypeName;
        std::function<bool(Entity)> has;
        std::function<Json(Entity)> encode;
        std::function<bool(Entity, const Json&)> decode;
        std::function<bool(Entity, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences;
        std::function<std::string(Entity)> getName;
        std::function<void(Entity, const std::string&)> setName;
        ComponentEditorBehavior editorBehavior;
        std::vector<ComponentProperty> editorProperties;
    };

    class IComponentCodecRegistry
    {
    public:
        virtual ~IComponentCodecRegistry() = default;
        virtual void RegisterCodec(ComponentCodec codec) = 0;
    };

    IComponentCodecRegistry& GetHostComponentCodecRegistry();

    enum class ModuleHost : uint32_t { Editor, Game, Server };
    inline constexpr uint32_t EngineModuleApiVersion = 4;

    struct EngineModuleContext
    {
        ModuleHost host = ModuleHost::Game;
        const char* moduleId = nullptr;
        void* hostServices = nullptr;
        const char* modulePathName = nullptr;
    };

    struct EngineModuleAPI
    {
        uint32_t apiVersion = EngineModuleApiVersion;
        const char* moduleId = nullptr;
        void (*registerComponentCodecs)(IComponentCodecRegistry&, const EngineModuleContext&) = nullptr;
        void (*initialize)(const EngineModuleContext&) = nullptr;
        void (*registerSystems)(World&, const EngineModuleContext&) = nullptr;
        void (*shutdown)(const EngineModuleContext&) = nullptr;
    };

    class EngineModuleManager
    {
    public:
        explicit EngineModuleManager(ModuleHost host, void* hostServices = nullptr);
        ~EngineModuleManager();
        EngineModuleManager(const EngineModuleManager&) = delete;
        EngineModuleManager& operator=(const EngineModuleManager&) = delete;

        bool LoadModule(const std::filesystem::path& path, std::string* error = nullptr);
        void LoadModulesFromDirectory(const std::filesystem::path& directory);
        void RegisterSystems(World& world);
        void UnloadAll();
        std::vector<std::string> GetLoadedModuleIds() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

#ifdef _WIN32
#define HY_ENGINE_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define HY_ENGINE_MODULE_EXPORT extern "C"
#endif
    using GetEngineModuleAPIFn = const EngineModuleAPI* (*)(uint32_t);
    void UnregisterCodecsByModule(const std::string& moduleId);

    std::vector<ComponentCodec>& GetComponentCodecs();
    void RegisterCodec(ComponentCodec codec);

    template<typename T>
    ComponentCodec GenerateComponentCodec(
        const std::string& type,
        const std::string& displayName,
        std::function<Json(const T&)> encode,
        std::function<bool(T&, const Json&)> decode,
        std::function<std::string(const T&)> getName = {},
        std::function<void(T&, const std::string&)> setName = {},
        std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences = {},
        uint32_t version = 1,
        std::vector<ComponentProperty> editorProperties = {},
        ComponentEditorBehavior editorBehavior = {}
    );

    template<typename T>
    void RegisterComponentCodec(
        const std::string& type,
        const std::string& displayName,
        std::function<Json(const T&)> encode,
        std::function<bool(T&, const Json&)> decode,
        std::function<std::string(const T&)> getName = {},
        std::function<void(T&, const std::string&)> setName = {},
        std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences = {},
        uint32_t version = 1,
        ComponentEditorBehavior editorBehavior = {});

    template<typename T>
    void RegisterComponentCodec(
        IComponentCodecRegistry& registry,
        const std::string& type,
        const std::string& displayName,
        std::function<Json(const T&)> encode,
        std::function<bool(T&, const Json&)> decode,
        std::function<std::string(const T&)> getName = {},
        std::function<void(T&, const std::string&)> setName = {},
        std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences = {},
        uint32_t version = 1,
        std::vector<ComponentProperty> editorProperties = {},
        ComponentEditorBehavior editorBehavior = {});

}

template<typename T>
NComponentCodec::ComponentCodec NComponentCodec::GenerateComponentCodec(
    const std::string& type,
    const std::string& displayName,
    std::function<Json(const T&)> encode,
    std::function<bool(T&, const Json&)> decode,
    std::function<std::string(const T&)> getName,
    std::function<void(T&, const std::string&)> setName,
    std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences,
    uint32_t version,
    std::vector<ComponentProperty> editorProperties,
    ComponentEditorBehavior editorBehavior)
{
    ComponentCodec codec;
    codec.type = type;
    codec.displayName = displayName.empty() ? type : displayName;
    codec.version = version;
    codec.componentType = ComponentType<T>::getId();
    codec.nativeTypeName = GetEcsNativeTypeName<T>();
    codec.has = [](Entity entity) { return entity.hasComponent<T>(); };
    codec.editorBehavior = std::move(editorBehavior);
    if (codec.editorBehavior.addable && !codec.editorBehavior.add)
        codec.editorBehavior.add = [](Entity entity) { entity.addComponent<T>(); };
    if (codec.editorBehavior.removable && !codec.editorBehavior.remove)
        codec.editorBehavior.remove = [](Entity entity) {
            auto* world = entity.getWorld();
            return world && world->removeComponentByTypeID(entity, ComponentType<T>::getId());
            };
    codec.encode = [encode = std::move(encode)](Entity entity) { return encode(entity.getComponent<T>()); };
    if (resolveReferences)
        codec.resolveReferences = [resolveReferences = std::move(resolveReferences)](Entity entity, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
        return resolveReferences(entity.getComponent<T>(), data, entities);
        };
    codec.decode = [decode = std::move(decode)](Entity entity, const Json& data) {
        T* existing = entity.tryGetComponent<T>();
        T component = existing ? *existing : T{};
        if (!decode(component, data))
            return false;
        if (existing)
            *existing = std::move(component);
        else
            entity.addComponent<T>(std::move(component));
        return true;
        };
    if (getName)
        codec.getName = [getName = std::move(getName)](Entity entity) {return getName(entity.getComponent<T>()); };
    if (setName)
        codec.setName = [setName = std::move(setName)](Entity entity, const std::string& name) {setName(entity.getComponent<T>(), name); };
    codec.editorProperties = std::move(editorProperties);
    return codec;
}

template<typename T>
void NComponentCodec::RegisterComponentCodec(
    const std::string& type,
    const std::string& displayName,
    std::function<Json(const T&)> encode,
    std::function<bool(T&, const Json&)> decode,
    std::function<std::string(const T&)> getName,
    std::function<void(T&, const std::string&)> setName,
    std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences,
    uint32_t version,
    ComponentEditorBehavior editorBehavior)
{
    ComponentCodec codec = GenerateComponentCodec<T>(type, displayName, encode, decode, getName, setName, resolveReferences, version, {}, std::move(editorBehavior));
    RegisterCodec(std::move(codec));
}

template<typename T>
void NComponentCodec::RegisterComponentCodec(
    IComponentCodecRegistry& registry,
    const std::string& type,
    const std::string& displayName,
    std::function<Json(const T&)> encode,
    std::function<bool(T&, const Json&)> decode,
    std::function<std::string(const T&)> getName,
    std::function<void(T&, const std::string&)> setName,
    std::function<bool(T&, const Json&, const std::unordered_map<std::string, Entity>&)> resolveReferences,
    uint32_t version,
    std::vector<ComponentProperty> editorProperties,
    ComponentEditorBehavior editorBehavior)
{
    ComponentCodec codec = GenerateComponentCodec<T>(type, displayName, encode, decode, getName, setName, resolveReferences, version, editorProperties, std::move(editorBehavior));
    registry.RegisterCodec(std::move(codec));
}
