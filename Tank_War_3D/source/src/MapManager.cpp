#include "Manager/MapManager.h"
#include "Manager/ResourceManager.h"

constexpr int DEF_BRICK_WIDTH = 56;
constexpr int DEF_BRICK_HEIGHT = 56;
constexpr int DEF_IRON_WIDTH = 56;
constexpr int DEF_IRON_HEIGHT = 56;

MapInfo Init_Map_1()
{
	return MapInfo();
}

MapInfo Init_Map_Debug()
{
	return MapInfo();
}

std::map<MapID, MapInfo> LoadDefMap()
{
	std::map<MapID, MapInfo> maps;
	maps[0] = Init_Map_Debug();
	maps[1] = Init_Map_1();

	return maps;
}

MapManager* MapManager::Instance()
{
	static MapManager* m_instance = new MapManager();
	return m_instance;
}

bool MapManager::isMapExist(MapID id)
{
	return _maps.find(id) != _maps.end();
}

MapInfo MapManager::getMap(MapID id)
{
	if (_maps.find(id) != _maps.end())
		return _maps[id];
	return MapInfo();
}

MapManager::MapManager()
{
	_maps = LoadDefMap();
}