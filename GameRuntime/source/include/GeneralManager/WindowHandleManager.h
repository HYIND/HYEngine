#pragma once

#include <windows.h>

class WindowHandleManager
{
public:
	static WindowHandleManager* Instance();

	void SetRect(RECT rect);
	void SetHwnd(HWND hwnd);
	void SetHinstance(HINSTANCE instance);

	RECT GetRect()const;
	HWND GetHwnd()const;
	HINSTANCE GetHinstance()const;

private:
	WindowHandleManager() = default;

private:
	RECT _rect;
	HWND _hwnd;
	HINSTANCE _hInstance;
};

#define WINDOWHANDLEMANAGER WindowHandleManager::Instance()