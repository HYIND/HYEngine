#include "Manager/MouseManager.h"

MouseManager* MouseManager::Instance()
{
	static MouseManager* m_instance = new MouseManager();
	return m_instance;
}

void MouseManager::ReSet(int X, int Y)
{
	LastMouseX = X;
	LastMouseY = Y;

	deltaX = 0;
	deltaY = 0;
}
#include <iostream>
void MouseManager::InputPos(int X, int Y)
{
	if (LastMouseX < 0 || X < 0)
		deltaX = 0;
	else
		deltaX = X - LastMouseX;

	if (LastMouseY < 0 || X < 0)
		deltaY = 0;
	else
		deltaY = Y - LastMouseY;

	LastMouseX = X;
	LastMouseY = Y;

	//std::cout << "deltaX=" << deltaY << " deltaY=" << deltaY << '\n';
}

void MouseManager::GetDelta(int& X, int& Y)
{
	X = deltaX;
	Y = deltaY;
}
