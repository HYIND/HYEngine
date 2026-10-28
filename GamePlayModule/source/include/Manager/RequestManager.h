#pragma once

#include "stdafx.h"
#include "Net/Helper/Buffer.h"
#include "Coroutine.h"

class RequestManager
{
public:
	static RequestManager* Instance();

public:
	void SendHallMsg(const std::string& msg);
	void SendRoomMsg(const std::string& msg);
	void SendPrivateMsg(const std::string& goaltoken, const std::string& msg);
	void RequestLobbyUserData();
	void RequestLobbyRoomData();

	void RequestRoomInfo();

	Task<bool> RequestCreateRoom();
	Task<bool> RequestJoinRoom(const std::string& roomid);
	Task<bool> RequestLeaveRoom();
	Task<bool> RequestChangeReadyStatus(bool isready);
	Task<bool> RequestStartGame(json& response);
	Task<bool> RequestLeaveGame();

private:
	RequestManager();
	~RequestManager();

};

#define REQUESTMANAGER RequestManager::Instance()