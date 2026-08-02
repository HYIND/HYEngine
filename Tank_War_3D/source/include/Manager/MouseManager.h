#pragma once

class MouseManager
{
public:
	static MouseManager* Instance();

	void ReSet(int X = -1, int Y = -1);
	void InputPos(int X, int Y);

	void GetDelta(int& X, int& Y);

private:
	MouseManager() = default;

private:
	int LastMouseX = -1;
	int LastMouseY = -1;

	int deltaX = 0;
	int deltaY = 0;
};