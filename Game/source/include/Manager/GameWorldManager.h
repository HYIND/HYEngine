#pragma once

#include "ECSCore/World.h"
#include <memory>
#include <thread>

class GameWorldManager
{
public:
	static GameWorldManager* Instance();

	std::shared_ptr<World> GetGameWorld();

	void InitGameWorld();
	void InitOnlineGameWorld();

	void RunWorld();
	void WorldLoop();
	void PauseWorld();
	void StopWorld();

	std::shared_ptr<std::thread> GetWorldThread();

public:
	void SyncFromServerState(const json& js);
	void SyncFromServerEvent(const json& js);
	void ProcessEliminateInfo(const json& js);
	void ProcessGameOver(const json& js);

private:
	GameWorldManager();

private:
	std::shared_ptr<World> _world;
	std::shared_ptr<std::thread> _worldThread;
	bool _stop;
};
