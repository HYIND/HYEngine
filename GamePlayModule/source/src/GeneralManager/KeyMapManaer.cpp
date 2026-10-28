#include "GeneralManager/KeyMapManaer.h"
#include <Windows.h>

int KeyMapManager::GetNumberVK(int number)
{
	return 0x30 + number;
}

int KeyMapManager::GetCharVK(char c)
{
	c = std::toupper(c);
	return 0x41 + (c - 'A');
}

KeyMapManager* KeyMapManager::Instance() {
	static KeyMapManager* instance = new KeyMapManager();
	return instance;
}

KeyMapManager::KeyMapManager() {
	_keymap[Keyboard::Forward] = GetCharVK('W');
	_keymap[Keyboard::Backward] = GetCharVK('S');
	_keymap[Keyboard::Left] = GetCharVK('A');
	_keymap[Keyboard::Right] = GetCharVK('D');
	_keymap[Keyboard::Up] = GetCharVK('Z');
	_keymap[Keyboard::Down] = GetCharVK('X');
	_keymap[Keyboard::Jump] = VK_SPACE;
	_keymap[Keyboard::Reload] = GetCharVK('R');
}

int KeyMapManager::GetKeyCode(Keyboard key) const {
	auto it = _keymap.find(key);
	if (it != _keymap.end()) {
		return it->second;
	}
	return -1;
}

void KeyMapManager::SetKeyCode(Keyboard key, int vkcode) {
	_keymap[key] = vkcode;
}

const std::map<Keyboard, int>& KeyMapManager::GetMap() const {
	return _keymap;
}