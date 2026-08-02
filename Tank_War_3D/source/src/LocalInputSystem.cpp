#include "stdafx.h"
#include "ECS/Systems/LocalInputSystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/PlayerInput.h"
#include "ECS/Components/Controller.h"
#include "Helper/keymap.h"
#include "Manager/FocusManager.h"
#include "Manager/MouseManager.h"
#include <Windows.h>

int GetNumberVK(int number)
{
	return 0x30 + number;
}

int GetCharVK(char c)
{
	c = std::toupper(c);
	return 0x41 + (c - 'A');
}

void GetKeyMap(int key[6])
{
	key[0] = key_map_p1[keybroad::UP];
	key[1] = key_map_p1[keybroad::DOWN];
	key[2] = key_map_p1[keybroad::LEFT];
	key[3] = key_map_p1[keybroad::RIGHT];
	key[4] = key_map_p1[keybroad::JUMP];
	key[5] = GetCharVK('R');
}

bool GetKeyPressState(int key)
{
	return GetAsyncKeyState(key) & 0x8000;
}

// 处理键盘输入
static void handlePlayerInput(PlayerInput& input, Controller& controller)
{
	if (!FocusManager::Instance()->ShouldProcessInput())
	{
		input.reset();
		return;
	}

	int keys[6];
	GetKeyMap(keys);

	input.setInput(PlayerInput::FORWARD, GetKeyPressState(keys[0]));
	input.setInput(PlayerInput::BACKWARD, GetKeyPressState(keys[1]));
	input.setInput(PlayerInput::LEFT, GetKeyPressState(keys[2]));
	input.setInput(PlayerInput::RIGHT, GetKeyPressState(keys[3]));
	input.setInput(PlayerInput::JUMP, GetKeyPressState(keys[4]));
	input.setInput(PlayerInput::RELOAD, GetKeyPressState(keys[5]));

	input.setInput(PlayerInput::FIRE, GetKeyPressState(VK_LBUTTON));

	input.setInput(PlayerInput::NUMBER_0, GetKeyPressState(GetNumberVK(0)));
	input.setInput(PlayerInput::NUMBER_1, GetKeyPressState(GetNumberVK(1)));
	input.setInput(PlayerInput::NUMBER_2, GetKeyPressState(GetNumberVK(2)));
	input.setInput(PlayerInput::NUMBER_3, GetKeyPressState(GetNumberVK(3)));
	input.setInput(PlayerInput::NUMBER_4, GetKeyPressState(GetNumberVK(4)));
	input.setInput(PlayerInput::NUMBER_5, GetKeyPressState(GetNumberVK(5)));
	input.setInput(PlayerInput::NUMBER_6, GetKeyPressState(GetNumberVK(6)));
	input.setInput(PlayerInput::NUMBER_7, GetKeyPressState(GetNumberVK(7)));
	input.setInput(PlayerInput::NUMBER_8, GetKeyPressState(GetNumberVK(8)));
	input.setInput(PlayerInput::NUMBER_9, GetKeyPressState(GetNumberVK(9)));

	MouseManager::Instance()->GetDelta(input.mouseDeltaX, input.mouseDeltaY);

	bool pressFORWARD = input.isPressed(PlayerInput::FORWARD);
	bool pressBACKWARD = input.isPressed(PlayerInput::BACKWARD);
	bool pressLEFT = input.isPressed(PlayerInput::LEFT);
	bool pressRIGHT = input.isPressed(PlayerInput::RIGHT);
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
