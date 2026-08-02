#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include <vector>
#include <map>
#include <iostream>
#include <cstdint>

namespace MapBoundary
{
	inline int left = 0;
	inline int right = 1000;
	inline int top = 0;
	inline int bottom = 600;
}

using MapID = uint32_t;

struct BrithInfo
{
	glm::vec3 position = glm::vec3(0.f);
	glm::quat rotation;

	BrithInfo(glm::vec3 pos, glm::quat rotation)
		:position(pos), rotation(rotation) {
	}
};
struct TankBirthInfo :public BrithInfo
{
	TankBirthInfo(glm::vec3 pos, glm::quat rotation)
		:BrithInfo(pos, rotation) {
	}
};

struct MapInfo
{
	std::vector<TankBirthInfo> tankbirthinfos;
};

class MapManager
{
public:
	static MapManager* Instance();

	bool isMapExist(MapID id);
	MapInfo getMap(MapID id);

private:
	MapManager();

private:
	std::map<MapID, MapInfo> _maps;
};