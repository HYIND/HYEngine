#include "Input/Systems/LocalInputSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GeneralManager/KeyMapManaer.h"
#include "GeneralManager/FocusManager.h"
#include "GeneralManager/MouseManager.h"
#include <Windows.h>

int FoundkKey(Keyboard key)
{
	return KeyMapManager::Instance()->GetKeyCode(key);
}

bool GetKeyPressState(int key)
{
	if (key < 0) return false;
	return GetAsyncKeyState(key) & 0x8000;
}

// 处理键盘输入
static void handlePlayerInput(PlayerInput& input, Controller& controller)
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

void LocalInputSystem::preUpdate(float deltaTime)
{
	auto& world = getWorld();
	std::vector<Entity> entities = world.getEntitiesWith<TagCurrentControl, PlayerInput, Controller>();
	for (auto& entity : entities)
	{
		auto& playerInput = world.getComponent<PlayerInput>(entity);
		auto& movement = world.getComponent<Controller>(entity);

		handlePlayerInput(playerInput, movement);
	}
}
