#pragma once

#include "ECSCore/World.h"

// 这里不再持有 World，也不再有自己的一条线程。
class GameWorldManager
{
public:
	static GameWorldManager* Instance();

	World* GetGameWorld();

	void InitGameWorld();
	void InitOnlineGameWorld();

public:
	void SyncFromServerState(const json& js);
	void SyncFromServerEvent(const json& js);
	void ProcessEliminateInfo(const json& js);
	void ProcessGameOver(const json& js);

private:
	GameWorldManager();

private:
	// 非拥有指针：World 的寿命由引擎管理，引擎关场景后这里会被置空。
	World* _world = nullptr;
};
