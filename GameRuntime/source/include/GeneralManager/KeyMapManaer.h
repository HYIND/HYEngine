#pragma once

#include <map>
#include <optional> 
#include <locale>

enum class Keyboard : int {
	Forward = 0,
	Backward = 1,
	Left = 2,
	Right = 3,
	Up = 4,
	Down = 5,
	Jump = 6,
	Reload = 7,
};

class KeyMapManager 
{
public:
	static int GetNumberVK(int number);		//方便获取对应键盘上按键的按键码
	static int GetCharVK(char c);			//方便获取对应键盘上按键的按键码

public:
	static KeyMapManager* Instance();

	KeyMapManager(const KeyMapManager&) = delete;
	KeyMapManager& operator=(const KeyMapManager&) = delete;

	int GetKeyCode(Keyboard key) const;
	void SetKeyCode(Keyboard key, int vkcode);
	const std::map<Keyboard, int>& GetMap() const;

private:
	KeyMapManager();
	~KeyMapManager() = default;

	std::map<Keyboard, int> _keymap;
};