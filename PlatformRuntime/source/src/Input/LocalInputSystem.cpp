#include "Input/Systems/LocalInputSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "Input/ControllerInput.h"


InputScriptModule::InputScriptModule(World* world) : _world(world) {}

void InputScriptModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
    RegisterInput(lua, entityType);
}

std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> InputScriptModule::ComponentRegister()
{
    std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> result;
    result["PlayerInput"] =
        [](World& world, Entity entity) {
        if (!entity.hasComponent<PlayerInput>())
            entity.addComponent<PlayerInput>();
        };
    return result;
}

void InputScriptModule::RegisterInput(sol::state_view lua, sol::usertype<Entity>& entityType)
{
    sol::table input = lua.create_table();

    auto withInput = [this](Entity entity) -> PlayerInput*
        {
            if (!_world || !IsValid(_world, entity))
                return nullptr;

            return entity.tryGetComponent<PlayerInput>();
        };

    auto isKeyDown = [withInput](Entity entity, const std::string& keyName) -> bool
        {
            auto* playerInput = withInput(entity);
            if (!playerInput)
                return false;

            return playerInput->IsDown(EngineKeyFromName(keyName));
        };

    auto isKeyPressed = [withInput](Entity entity, const std::string& keyName) -> bool
        {
            auto* playerInput = withInput(entity);
            if (!playerInput)
                return false;

            return playerInput->WasPressed(EngineKeyFromName(keyName));
        };

    auto isKeyReleased = [withInput](Entity entity, const std::string& keyName) -> bool
        {
            auto* playerInput = withInput(entity);
            if (!playerInput)
                return false;

            return playerInput->WasReleased(EngineKeyFromName(keyName));
        };

    input.set_function("is_key_down", isKeyDown);
    input.set_function("is_key_pressed", isKeyPressed);
    input.set_function("is_key_released", isKeyReleased);
    lua["Input"] = input;

    entityType.set("is_key_down", isKeyDown);
    entityType.set("is_key_pressed", isKeyPressed);
    entityType.set("is_key_released", isKeyReleased);
}

void LocalInputSystem::onAttach(World& world)
{
    if (auto scriptSystem = world.getSystem<ScriptSystem>())
    {
        scriptSystem->RegisterScriptModule("LocalInputSystem", std::make_unique<InputScriptModule>(&world));
    }
}

LocalInputSystem::LocalInputSystem(std::shared_ptr<EngineInputContext> inputContext)
    : m_inputContext(std::move(inputContext))
{}

void LocalInputSystem::preUpdate(float deltaTime)
{
    auto inputContext = m_inputContext;
    if (!inputContext)
        return;

    auto& world = getWorld();
    auto views = world.getViewWith<TagCurrentControl, PlayerInput>();
    const EngineInputFrame frame = inputContext->ConsumeFrame();

    for (auto [entity, tag, playerInput] : views)
    {
        SyncPlayerInput(playerInput, frame);

        if (auto controller = entity.tryGetComponent<Controller>())
            ApplyDefaultControllerInput(playerInput, *controller);
    }
}
