#include "GeneralManager/WindowHandleManager.h"

WindowHandleManager* WindowHandleManager::Instance() {
	static WindowHandleManager* instance = new WindowHandleManager();
	return instance;
}

void WindowHandleManager::SetRect(RECT rect)
{
	_rect = rect;
}

void WindowHandleManager::SetHwnd(HWND hwnd)
{
	_hwnd = hwnd;
}

void WindowHandleManager::SetHinstance(HINSTANCE instance)
{
	_hInstance = instance;
}

RECT WindowHandleManager::GetRect() const { return  _rect; }

HWND WindowHandleManager::GetHwnd() const { return _hwnd; }

HINSTANCE WindowHandleManager::GetHinstance()const { return _hInstance; }
