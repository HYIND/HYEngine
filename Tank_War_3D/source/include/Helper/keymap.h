#pragma once

#include <map>

enum class keybroad {
	UP = 0,
	DOWN = 1,
	LEFT = 2,
	RIGHT = 3,
	JUMP = 4
};

extern std::map<enum class keybroad, int> key_map_p1;

extern std::map<enum class keybroad, int> key_map_p2;