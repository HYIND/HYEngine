#include "GamePlayModule.h"

#include "GamePlayComponents.h"
#include "CommonComponent.h"
#include "EngineSystemPriority.h"
#include "GamePlaySystems.h"
#include "SceneCodec/ComponentCodec.h"
#include "SceneCodec/ModuleHostServices.h"
#include "UI/UICommandBus.h"

#include <cmath>

namespace
{
    using NComponentCodec::Json;

    std::string EntityKey(Entity entity)
    {
        return entity ? std::to_string(entity.getId()) : std::string{};
    }

    bool ResolveEntity(const Json& value, const std::unordered_map<std::string, Entity>& entities, Entity& out)
    {
        if (!value.is_string())
            return false;
        const auto key = value.get<std::string>();
        if (key.empty())
        {
            out = Entity{};
            return true;
        }
        const auto found = entities.find(key);
        if (found == entities.end())
            return false;
        out = found->second;
        return true;
    }

    template<typename T>
    void RegisterEmptyComponent(NComponentCodec::IComponentCodecRegistry& registry, const std::string& type, const std::string& name)
    {
        NComponentCodec::RegisterComponentCodec<T>(
            registry,
            type,
            name,
            [](const T&) { return Json::object(); },
            [](T&, const Json& data) { return data.is_object(); });
    }
}

static void RegisterGamePlayCodecs(NComponentCodec::IComponentCodecRegistry& registry, const NComponentCodec::EngineModuleContext&)
{
    using NComponentCodec::Json;

    NComponentCodec::RegisterComponentCodec<Health>(
        registry,
        "gameplay.health",
        "Health",
        [](const Health& value) {
            return Json{ {"currentHealth", value.currentHealth}, {"maxHealth", value.maxHealth}, {"isInvulnerable", value.isInvulnerable} };
        },
        [](Health& value, const Json& data) {
            if (!data.is_object() || !data.contains("currentHealth") || !data["currentHealth"].is_number_integer()
                || !data.contains("maxHealth") || !data["maxHealth"].is_number_integer()
                || !data.contains("isInvulnerable") || !data["isInvulnerable"].is_boolean())
                return false;
            value.currentHealth = data["currentHealth"].get<int>();
            value.maxHealth = data["maxHealth"].get<int>();
            value.isInvulnerable = data["isInvulnerable"].get<bool>();
            return value.maxHealth >= 0 && value.currentHealth >= 0 && value.currentHealth <= value.maxHealth;
        }, {}, {}, {}, 1, {
            NComponentCodec::MakeComponentProperty<Health>("currentHealth", "Current Health", &Health::currentHealth, NComponentCodec::EditorPropertyKind::Integer, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<Health>("maxHealth", "Maximum Health", &Health::maxHealth, NComponentCodec::EditorPropertyKind::Integer, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<Health>("isInvulnerable", "Invulnerable", &Health::isInvulnerable, NComponentCodec::EditorPropertyKind::Boolean)
        }
            );

    NComponentCodec::RegisterComponentCodec<LightFollow>(registry,
        "gameplay.light_follow",
        "LightFollow",
        [](const LightFollow& value) { return Json{ {"target", EntityKey(value.target)} }; },
        [](LightFollow&, const Json& data) { return data.is_object() && data.contains("target") && data["target"].is_string(); },
        {}, {},
        [](LightFollow& value, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
            return ResolveEntity(data.at("target"), entities, value.target);
        });

    NComponentCodec::RegisterComponentCodec<LightShow>(registry,
        "gameplay.light_show",
        "LightShow",
        [](const LightShow& value) {
            return Json{ {"exposure", value.exposure}, {"threshold", value.thresold}, {"lossRate", value.lossRate}, {"isExcited", value.isExcited} };
        },
        [](LightShow& value, const Json& data) {
            if (!data.is_object() || !data.contains("exposure") || !data["exposure"].is_number()
                || !data.contains("threshold") || !data["threshold"].is_number()
                || !data.contains("lossRate") || !data["lossRate"].is_number()
                || !data.contains("isExcited") || !data["isExcited"].is_boolean())
                return false;
            value.exposure = data["exposure"].get<float>();
            value.thresold = data["threshold"].get<float>();
            value.lossRate = data["lossRate"].get<float>();
            value.isExcited = data["isExcited"].get<bool>();
            return std::isfinite(value.exposure) && std::isfinite(value.thresold) && std::isfinite(value.lossRate);
        }, {}, {}, {}, 1, {
            NComponentCodec::MakeComponentProperty<LightShow>("exposure", "Exposure", &LightShow::exposure, NComponentCodec::EditorPropertyKind::Float, false, 0, 1000000, 0.1),
            NComponentCodec::MakeComponentProperty<LightShow>("threshold", "Threshold", &LightShow::thresold, NComponentCodec::EditorPropertyKind::Float, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<LightShow>("lossRate", "Loss Rate", &LightShow::lossRate, NComponentCodec::EditorPropertyKind::Float, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<LightShow>("isExcited", "Excited", &LightShow::isExcited, NComponentCodec::EditorPropertyKind::Boolean, true)
        }
            );

    NComponentCodec::RegisterComponentCodec<WeaponBasic>(registry,
        "gameplay.weapon_basic",
        "WeaponBasic",
        [](const WeaponBasic& value) {
            return Json{ {"weaponName", value.weaponName}, {"damage", value.damage}, {"maxAmmo", value.maxAmmo},
                {"maxReserveAmmo", value.maxReserveAmmo}, {"fireRate", value.fireRate}, {"reloadTime", value.reloadTime},
                {"range", value.range}, {"fireMode", static_cast<int>(value.fireMode)} };
        },
        [](WeaponBasic& value, const Json& data) {
            if (!data.is_object() || !data.contains("weaponName") || !data["weaponName"].is_string()
                || !data.contains("damage") || !data["damage"].is_number_integer()
                || !data.contains("maxAmmo") || !data["maxAmmo"].is_number_integer()
                || !data.contains("maxReserveAmmo") || !data["maxReserveAmmo"].is_number_integer()
                || !data.contains("fireRate") || !data["fireRate"].is_number_integer()
                || !data.contains("reloadTime") || !data["reloadTime"].is_number_integer()
                || !data.contains("range") || !data["range"].is_number()
                || !data.contains("fireMode") || !data["fireMode"].is_number_integer())
                return false;
            const int mode = data["fireMode"].get<int>();
            if (mode < WeaponBasic::Single || mode > WeaponBasic::Burst)
                return false;
            value.weaponName = data["weaponName"].get<std::string>();
            value.damage = data["damage"].get<int>();
            value.maxAmmo = data["maxAmmo"].get<int>();
            value.maxReserveAmmo = data["maxReserveAmmo"].get<int>();
            value.fireRate = data["fireRate"].get<int64_t>();
            value.reloadTime = data["reloadTime"].get<int64_t>();
            value.range = data["range"].get<float>();
            value.fireMode = static_cast<WeaponBasic::Mode>(mode);
            return std::isfinite(value.range);
        }, {}, {}, {}, 1, {
            NComponentCodec::MakeComponentProperty<WeaponBasic>("weaponName", "Weapon Name", &WeaponBasic::weaponName, NComponentCodec::EditorPropertyKind::String),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("damage", "Damage", &WeaponBasic::damage, NComponentCodec::EditorPropertyKind::Integer, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("maxAmmo", "Magazine Capacity", &WeaponBasic::maxAmmo, NComponentCodec::EditorPropertyKind::Integer, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("maxReserveAmmo", "Reserve Capacity", &WeaponBasic::maxReserveAmmo, NComponentCodec::EditorPropertyKind::Integer, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("fireRate", "Fire Interval (ms)", &WeaponBasic::fireRate, NComponentCodec::EditorPropertyKind::Integer64, false, 0, 0, 10),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("reloadTime", "Reload Time (ms)", &WeaponBasic::reloadTime, NComponentCodec::EditorPropertyKind::Integer64, false, 0, 0, 10),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("range", "Range", &WeaponBasic::range, NComponentCodec::EditorPropertyKind::Float, false, 0, 1000000, 1),
            NComponentCodec::MakeComponentProperty<WeaponBasic>("fireMode", "Fire Mode", &WeaponBasic::fireMode, NComponentCodec::EditorPropertyKind::Enumeration, false, 0, 0, 1,
                {{WeaponBasic::Single, "Single"}, {WeaponBasic::Auto, "Automatic"}, {WeaponBasic::Burst, "Burst"}})
        }
            );

    NComponentCodec::RegisterComponentCodec<WeaponState>(registry,
        "gameplay.weapon_state",
        "WeaponState",
        [](const WeaponState& value) {
            return Json{ {"currentAmmo", value.currentAmmo}, {"reserveAmmo", value.reserveAmmo}, {"lastFireTime", value.lastFireTime},
                {"reloadTimer", value.reloadTimer}, {"isReloading", value.isReloading} };
        },
        [](WeaponState& value, const Json& data) {
            if (!data.is_object() || !data.contains("currentAmmo") || !data["currentAmmo"].is_number_integer()
                || !data.contains("reserveAmmo") || !data["reserveAmmo"].is_number_integer()
                || !data.contains("lastFireTime") || !data["lastFireTime"].is_number_integer()
                || !data.contains("reloadTimer") || !data["reloadTimer"].is_number_integer()
                || !data.contains("isReloading") || !data["isReloading"].is_boolean())
                return false;
            value.currentAmmo = data["currentAmmo"].get<int>();
            value.reserveAmmo = data["reserveAmmo"].get<int>();
            value.lastFireTime = data["lastFireTime"].get<int64_t>();
            value.reloadTimer = data["reloadTimer"].get<int64_t>();
            value.isReloading = data["isReloading"].get<bool>();
            return value.currentAmmo >= 0 && value.reserveAmmo >= 0 && value.reloadTimer >= 0;
        }
    );

    NComponentCodec::RegisterComponentCodec<WeaponOwner>(registry,
        "gameplay.weapon_owner",
        "WeaponOwner",
        [](const WeaponOwner& value) {
            Json weapons = Json::array();
            for (auto weapon : value.weapons)
                weapons.push_back(EntityKey(weapon));
            return Json{ {"weapons", std::move(weapons)}, {"switchCoolDown", value.switchCoolDown},
                {"curSwitchCoolDown", value.curSwitchCoolDown}, {"currentIndex", value.currentIndex},
                {"weaponSocket", EntityKey(value.weaponSocket)} };
        },
        [](WeaponOwner& value, const Json& data) {
            return data.is_object() && data.contains("weapons") && data["weapons"].is_array()
                && data.contains("switchCoolDown") && data["switchCoolDown"].is_number()
                && data.contains("curSwitchCoolDown") && data["curSwitchCoolDown"].is_number()
                && data.contains("currentIndex") && data["currentIndex"].is_number_integer()
                && data.contains("weaponSocket") && data["weaponSocket"].is_string();
        },
        {}, {},
        [](WeaponOwner& value, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
            value.weapons.clear();
            for (const auto& key : data.at("weapons"))
            {
                Entity weapon;
                if (!ResolveEntity(key, entities, weapon) || !weapon)
                    return false;
                value.weapons.push_back(weapon);
            }
            value.switchCoolDown = data.at("switchCoolDown").get<float>();
            value.curSwitchCoolDown = data.at("curSwitchCoolDown").get<float>();
            value.currentIndex = data.at("currentIndex").get<int>();
            if (!ResolveEntity(data.at("weaponSocket"), entities, value.weaponSocket))
                return false;
            return std::isfinite(value.switchCoolDown) && std::isfinite(value.curSwitchCoolDown);
        });

    RegisterEmptyComponent<TagLightShowLight>(registry, "gameplay.tag_light_show_light", "TagLightShowLight");
    RegisterEmptyComponent<TagLightShowEntity>(registry, "gameplay.tag_light_show_entity", "TagLightShowLightEntity");
    RegisterEmptyComponent<TagPlayer>(registry, "gameplay.tag_player", "TagPlayer");
    RegisterEmptyComponent<TagCharacter>(registry, "gameplay.tag_character", "TagCharacter");
    RegisterEmptyComponent<TagWeapon>(registry, "gameplay.tag_weapon", "TagWeapon");
}

static void RegisterGamePlayModuleSystems(World& world, const NComponentCodec::EngineModuleContext& context)
{
    // These are module-owned priorities. The Engine enum only supplies
    // scheduling anchors; project systems choose positions relative to them.
    constexpr int movementPriority = ToSystemPriority(EngineSystemPriority::Input) - 500;
    constexpr int weaponPriority = ToSystemPriority(EngineSystemPriority::Script) - 50;
    constexpr int lightFollowPriority = ToSystemPriority(EngineSystemPriority::CameraFollow) - 100;
    constexpr int mapBoundaryPriority = ToSystemPriority(EngineSystemPriority::Physics) + 50;

    auto& movement = world.registerSystem<MovementSystem>();
    auto& health = world.registerSystem<HealthSystem>();
    auto& cameraFollow = world.registerSystem<CameraFollowSystem>();
    auto& weapon = world.registerSystem<WeaponSystem>();
    auto& lightShow = world.registerSystem<LightShowSystem>();
    auto& lightFollow = world.registerSystem<LightFollowSystem>();

    movement.setPriority(movementPriority);
    weapon.setPriority(weaponPriority);
    cameraFollow.setPriority(ToSystemPriority(EngineSystemPriority::CameraFollow));
    lightFollow.setPriority(lightFollowPriority);

    (void)health;
    (void)lightShow;

    if (context.host == NComponentCodec::ModuleHost::Game)
    {
        auto& mapBoundary = world.registerSystem<MapBoundarySystem>();
        mapBoundary.SetBoundaryHeight(-60.f);
        mapBoundary.setPriority(mapBoundaryPriority);
    }
}

namespace
{
    // 模块只加载一次，所以在 initialize 里存一份宿主服务。卸载时引擎先 UnregisterByModule
    // 再 FreeLibrary，不会留下指向已卸载代码的回调。
    const NComponentCodec::ModuleHostServices* g_hostServices = nullptr;
}

// UI 命令的处理者：在场景线程上被调用，所以可以直接读写世界。
static void OnGamePlayUiCommand(const UI::UiEvent& event, World& world)
{
    (void)world;

    // command 用模块自己的命名空间前缀，模块只处理自己这一片。
    if (event.command != "gameplay.scene.load")
        return;

    // 切场景只能"请求"：模块不拥有线程，也不拥有 World 的生死，由引擎在宿主帧边界执行。
    // 载荷放 event.text（textinput 提交的内容也走这里）。
    if (!g_hostServices || !g_hostServices->requestSceneChange || event.text.empty())
        return;
    g_hostServices->requestSceneChange(g_hostServices->sceneServiceOwner, event.text.c_str());
}

static void InitializeGamePlayModule(const NComponentCodec::EngineModuleContext& context)
{
    auto* services = static_cast<NComponentCodec::ModuleHostServices*>(context.hostServices);
    if (!services || !services->uiCommands || !services->requestSceneChange)
        return;   // 宿主没开 UI 服务：静默降级，模块其它功能照常

    g_hostServices = services;

    // 对应 .ui 里的 "onClick": { "command": "gameplay.scene.load" }。
    services->uiCommands->Register(context.moduleId, "gameplay.scene.load", &OnGamePlayUiCommand);
}

HY_ENGINE_MODULE_EXPORT const NComponentCodec::EngineModuleAPI* GetEngineModuleAPI(uint32_t requestedApiVersion)
{
    static const NComponentCodec::EngineModuleAPI api{
        NComponentCodec::EngineModuleApiVersion,
        "hy.gameplay",
        &RegisterGamePlayCodecs,
        &InitializeGamePlayModule,
        &RegisterGamePlayModuleSystems,
        nullptr
    };
    return requestedApiVersion == api.apiVersion ? &api : nullptr;
}
