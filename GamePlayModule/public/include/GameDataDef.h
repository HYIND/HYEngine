#pragma once

#include <iostream>

using PlayerID = std::string;
using PlayerName = std::string;
using GameID = std::string;

struct GamePlayer
{
	PlayerID playerid;
	PlayerName name;
};