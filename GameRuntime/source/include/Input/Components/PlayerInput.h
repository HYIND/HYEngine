#pragma once

#include <stdint.h>
#include <bitset>
#include "ECSCore/IComponent.h"

struct PlayerInput :public IComponent
{
	enum InputState {
		FORWARD = 0,
		BACKWARD,
		LEFT,
		RIGHT,
		UP,
		DOWN,
		JUMP,
		FIRE,
		RELOAD,
		NUMBER_0,
		NUMBER_1,
		NUMBER_2,
		NUMBER_3,
		NUMBER_4,
		NUMBER_5,
		NUMBER_6,
		NUMBER_7,
		NUMBER_8,
		NUMBER_9,
		MAX_INPUTS
	};

	std::bitset<MAX_INPUTS> inputState;

	int mouseDeltaX = 0;
	int mouseDeltaY = 0;

	PlayerInput() {}

	void setInput(InputState state, bool pressed) {
		inputState.set(state, pressed);
	}

	void setMouseInput(int deltaX, int deltaY)
	{
		mouseDeltaX = deltaX;
		mouseDeltaY = deltaY;
	}

	bool isPressed(InputState state) const {
		return inputState.test(state);
	}

	void reset()
	{
		inputState.reset();
		mouseDeltaX = 0;
		mouseDeltaY = 0;
	}
};