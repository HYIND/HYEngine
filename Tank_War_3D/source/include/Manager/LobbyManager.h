#pragma once

#include "stdafx.h"
#include "LobbyDataDef.h"
#include "Coroutine.h"

class LobbyManager
{
public:
	static LobbyManager* Instance();

public:
	~LobbyManager();
	void ProcessMsg(const json& js);

public:
	void ProcessLobbyUserData(const json& js);
	void ProcessLobbyRoomData(const json& js);
	void ProcessHallMsg(const json& js);
	void ProcessRoomMsg(const json& js);
	void ProcessPrivateMsg(const json& js);
	void ProcessSendMsgReceipt(const json& js);
	void ProcessRoomInfo(const json& js);
	Task<void> ProcessStartGameRes(const json& js);

public:
	std::shared_ptr<Room> GetSelectRoom();
	Task<bool> TryJoinRoom(std::shared_ptr<Room> room);
	Task<bool> TryCreateRoom();
	Task<bool> TryLeaveRoom();
	Task<bool> TryChangeReadyStatus(bool isready);
	Task<bool> TryStartGame();

	void SetIsHost(bool value) { ishost = value; }
	bool IsHost() { return ishost; }

private:
	LobbyManager();

private:
	std::vector<Room*> _currooms;
	CoroCriticalSectionLock _curroomsmutex;

	bool ishost;
	std::shared_ptr<Room> _curroominfo;
};

#define LOBBYMANAGER LobbyManager::Instance()