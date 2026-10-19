#include "Input/Systems/LocalInputSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GeneralManager/KeyMapManaer.h"
#include "GeneralManager/FocusManager.h"
#include "GeneralManager/MouseManager.h"
#include <Windows.h>


InputScriptModule::InputScriptModule(World* world) : _world(world) {}

void InputScriptModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	RegisterInput(lua, entityType);
	RegisterController(lua, entityType);
}

std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> InputScriptModule::ComponentRegister()
{
	std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> result;
	result["PlayerInput"] =
		[](World& world, Entity entity) {
		if (!entity.hasComponent<PlayerInput>())
			entity.addComponent<PlayerInput>();
		};
	result["Controller"] =
		[](World& world, Entity entity) {
		if (!entity.hasComponent<Controller>())
			entity.addComponent<Controller>();
		};
	return result;
}

void InputScriptModule::RegisterInput(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	sol::table input = lua.create_table();

	auto ispressed = [this](Entity entity, const std::string& str) -> bool
		{
			if (!_world || !IsValid(_world, entity))
				return false;

			auto pPlayerInput = entity.tryGetComponent<PlayerInput>();
			if (!pPlayerInput)
				return false;

			auto& playerInput = *pPlayerInput;

			auto name = Tool::ToUpper(str);
			if (name == "FORWARD")
				return playerInput.isPressed(PlayerInput::FORWARD);
			if (name == "BACKWARD")
				return playerInput.isPressed(PlayerInput::BACKWARD);
			if (name == "LEFT")
				return playerInput.isPressed(PlayerInput::LEFT);
			if (name == "RIGHT")
				return playerInput.isPressed(PlayerInput::RIGHT);
			if (name == "UP")
				return playerInput.isPressed(PlayerInput::UP);
			if (name == "DOWN")
				return playerInput.isPressed(PlayerInput::DOWN);
			if (name == "JUMP")
				return playerInput.isPressed(PlayerInput::JUMP);
			if (name == "FIRE")
				return playerInput.isPressed(PlayerInput::FIRE);
			if (name == "RELOAD")
				return playerInput.isPressed(PlayerInput::RELOAD);

			return false;
		};

	input.set_function("is_pressed", ispressed);
	lua["Input"] = input;

	entityType.set("is_pressed", ispressed);
}

void InputScriptModule::RegisterController(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	auto getForwardBackward = [this](Entity entity) -> int
		{
			if (!_world || !IsValid(_world, entity) ||
				!_world->hasComponent<Controller>(entity))
				return -1;

			auto& c = _world->getComponent<Controller>(entity);
			return static_cast<int>(c.forwardBackward);
		};

	auto getLeftRight = [this](Entity entity) -> int
		{
			if (!_world || !IsValid(_world, entity) ||
				!_world->hasComponent<Controller>(entity))
				return -1;

			auto& c = _world->getComponent<Controller>(entity);
			return static_cast<int>(c.leftRight);
		};

	auto getUpDown = [this](Entity entity) -> int
		{
			if (!_world || !IsValid(_world, entity) ||
				!_world->hasComponent<Controller>(entity))
				return -1;

			auto& c = _world->getComponent<Controller>(entity);
			return static_cast<int>(c.upDown);
		};

	auto setWantToJump = [this](Entity entity, bool value)
		{
			if (!_world || !IsValid(_world, entity) ||
				!_world->hasComponent<Controller>(entity))
				return;

			auto& c = _world->getComponent<Controller>(entity);
			c.setWantToJump(value);
		};

	auto setWantToFire = [this](Entity entity, bool value)
		{
			if (!_world || !IsValid(_world, entity) ||
				!_world->hasComponent<Controller>(entity))
				return;

			auto& c = _world->getComponent<Controller>(entity);
			c.setWantToFire(value);
		};

	// 保留原有的 Controller table
	sol::table controller = lua.create_table();

	controller.set_function("get_forward_backward", getForwardBackward);
	controller.set_function("get_left_right", getLeftRight);
	controller.set_function("get_up_down", getUpDown);
	controller.set_function("set_want_to_jump", setWantToJump);
	controller.set_function("set_want_to_fire", setWantToFire);

	lua["Controller"] = controller;

	// 同时注册到 Entity
	entityType.set("get_forward_backward", getForwardBackward);
	entityType.set("get_left_right", getLeftRight);
	entityType.set("get_up_down", getUpDown);
	entityType.set("set_want_to_jump", setWantToJump);
	entityType.set("set_want_to_fire", setWantToFire);
}

static int FoundkKey(Keyboard key)
{
	return KeyMapManager::Instance()->GetKeyCode(key);
}

static bool GetKeyPressState(int key)
{
	if (key < 0) return false;
	return GetAsyncKeyState(key) & 0x8000;
}

static void handlePlayerInput(PlayerInput& input)
{
	if (FocusManager::Instance()->ShouldProcessInput())
	{
		input.setInput(PlayerInput::FORWARD, GetKeyPressState(FoundkKey(Keyboard::Forward)));
		input.setInput(PlayerInput::BACKWARD, GetKeyPressState(FoundkKey(Keyboard::Backward)));
		input.setInput(PlayerInput::LEFT, GetKeyPressState(FoundkKey(Keyboard::Left)));
		input.setInput(PlayerInput::RIGHT, GetKeyPressState(FoundkKey(Keyboard::Right)));
		input.setInput(PlayerInput::UP, GetKeyPressState(FoundkKey(Keyboard::Up)));
		input.setInput(PlayerInput::DOWN, GetKeyPressState(FoundkKey(Keyboard::Down)));
		input.setInput(PlayerInput::JUMP, GetKeyPressState(FoundkKey(Keyboard::Jump)));
		input.setInput(PlayerInput::RELOAD, GetKeyPressState(FoundkKey(Keyboard::Reload)));

		input.setInput(PlayerInput::FIRE, GetKeyPressState(VK_LBUTTON));

		input.setInput(PlayerInput::NUMBER_0, GetKeyPressState(KeyMapManager::GetNumberVK(0)));
		input.setInput(PlayerInput::NUMBER_1, GetKeyPressState(KeyMapManager::GetNumberVK(1)));
		input.setInput(PlayerInput::NUMBER_2, GetKeyPressState(KeyMapManager::GetNumberVK(2)));
		input.setInput(PlayerInput::NUMBER_3, GetKeyPressState(KeyMapManager::GetNumberVK(3)));
		input.setInput(PlayerInput::NUMBER_4, GetKeyPressState(KeyMapManager::GetNumberVK(4)));
		input.setInput(PlayerInput::NUMBER_5, GetKeyPressState(KeyMapManager::GetNumberVK(5)));
		input.setInput(PlayerInput::NUMBER_6, GetKeyPressState(KeyMapManager::GetNumberVK(6)));
		input.setInput(PlayerInput::NUMBER_7, GetKeyPressState(KeyMapManager::GetNumberVK(7)));
		input.setInput(PlayerInput::NUMBER_8, GetKeyPressState(KeyMapManager::GetNumberVK(8)));
		input.setInput(PlayerInput::NUMBER_9, GetKeyPressState(KeyMapManager::GetNumberVK(9)));

		MouseManager::Instance()->GetDelta(input.mouseDeltaX, input.mouseDeltaY);
	}
	else
		input.reset();
}

// 处理键盘输入
static void handleController(PlayerInput& input, Controller& controller)
{
	bool pressFORWARD = input.isPressed(PlayerInput::FORWARD);
	bool pressBACKWARD = input.isPressed(PlayerInput::BACKWARD);
	bool pressLEFT = input.isPressed(PlayerInput::LEFT);
	bool pressRIGHT = input.isPressed(PlayerInput::RIGHT);
	bool pressUP = input.isPressed(PlayerInput::UP);
	bool pressDOWN = input.isPressed(PlayerInput::DOWN);
	bool pressJUMP = input.isPressed(PlayerInput::JUMP);
	bool pressFIRE = input.isPressed(PlayerInput::FIRE);
	bool pressReload = input.isPressed(PlayerInput::RELOAD);

	if (pressFORWARD == pressBACKWARD)
		controller.setForwardBackward(Controller::ForwardBackward::NONE);
	else
	{
		if (pressFORWARD) 		controller.setForwardBackward(Controller::ForwardBackward::FORWARD);
		else if (pressBACKWARD) controller.setForwardBackward(Controller::ForwardBackward::BACKWARD);
	}

	if (pressLEFT == pressRIGHT)
		controller.setLeftRight(Controller::LeftRight::NONE);
	else
	{
		if (pressLEFT) 		controller.setLeftRight(Controller::LeftRight::LEFT);
		else if (pressRIGHT) controller.setLeftRight(Controller::LeftRight::RIGHT);
	}

	if (pressUP == pressDOWN)
		controller.setUpDown(Controller::UpDown::NONE);
	else
	{
		if (pressUP) 		controller.setUpDown(Controller::UpDown::UP);
		else if (pressDOWN) controller.setUpDown(Controller::UpDown::DOWN);
	}

	controller.setWantToJump(pressJUMP);
	controller.setWantToFire(pressFIRE);
	controller.setWantToReload(pressReload);

	int numberPress = -1;
	for (int i = 0; i <= 9; i++)
	{
		if (input.isPressed((PlayerInput::InputState)(PlayerInput::NUMBER_0 + i)))
		{
			numberPress = i;
			break;
		}
	}
	controller.setNumberPress(numberPress);
	controller.addMouseDelta(input.mouseDeltaX, input.mouseDeltaY);
}

void LocalInputSystem::onAttach(World& world)
{
	if (auto scriptSystem = world.getSystem<ScriptSystem>())
	{
		scriptSystem->RegisterScriptModule("LocalInputSystem", std::make_unique<InputScriptModule>(&world));
	}
}

void LocalInputSystem::preUpdate(float deltaTime)
{
	auto& world = getWorld();
	auto views = world.getViewWith<TagCurrentControl, PlayerInput>();
	for (auto [entity, tag, playerInput] : views)
	{
		handlePlayerInput(playerInput);
		if (auto controller = entity.tryGetComponent<Controller>())
			handleController(playerInput, *controller);
	}
}
