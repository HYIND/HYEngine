#include "Helper/keymap.h"

#include "framework.h"

std::map<enum class keybroad, int> key_map_p1 = {
	{ keybroad::UP,87 },{ keybroad::DOWN,83 },
	{ keybroad::LEFT,65 },{ keybroad::RIGHT,68 },
	{ keybroad::JUMP,VK_SPACE }
};

std::map<enum class keybroad, int> key_map_p2 = {
	{ keybroad::UP,VK_UP },{ keybroad::DOWN,VK_DOWN },
	{ keybroad::LEFT,VK_LEFT },{ keybroad::RIGHT,VK_RIGHT },
	{ keybroad::JUMP,VK_OEM_2 }
};