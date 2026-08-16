#include "Manager/RequestManager.h"
#include "Manager/ConnectManager.h"
#include "Manager/UserInfoManager.h"
#include "command.h"

using namespace LobbySubServiceCommand;

RequestManager* RequestManager::Instance()
{
	static RequestManager* instance = new RequestManager();
	return instance;
}

RequestManager::RequestManager()
{
}
RequestManager::~RequestManager()
{
}

void RequestManager::SendHallMsg(const std::string& msg)
{
	json js;
	js["command"] = LobbySubService_SendHallMsg;
	js["msg"] = msg;

	json resp;
	if (!ConnectManager::Instance()->SocialSend(js))
		return;
}

void RequestManager::SendRoomMsg(const std::string& msg)
{
	json js;
	js["command"] = LobbySubService_SendRoomMsg;
	js["msg"] = msg;

	if (ConnectManager::Instance()->SocialSend(js))
		return;
}

void RequestManager::SendPrivateMsg(const std::string& goaltoken, const std::string& msg)
{
	json js;
	js["command"] = LobbySubService_PrivateMsgForward;
	js["goaltoken"] = goaltoken;
	js["msg"] = msg;

	if (!ConnectManager::Instance()->SocialSend(js))
		return;

}

void RequestManager::RequestLobbyUserData()
{
	json js;
	js["command"] = LobbySubService_RequestLobbyUser;

	ConnectManager::Instance()->SocialSend(js);
}

void RequestManager::RequestLobbyRoomData()
{
	json js;
	js["command"] = LobbySubService_RequestLobbyRoom;

	ConnectManager::Instance()->SocialSend(js);
}

void RequestManager::RequestRoomInfo()
{
	json js;
	js["command"] = LobbySubService_RequestRoomInfo;

	ConnectManager::Instance()->SocialSend(js);
}

Task<bool> RequestManager::RequestCreateRoom()
{
	json js;
	js["command"] = LobbySubService_RequestCreateRoom;

	json js_resp;
	if (!co_await ConnectManager::Instance()->SocialRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "LobbySubService_RequestJoinRoom Request fail! reason:" << reason << '\n';
		co_return false;
	}

	co_return true;
}

Task<bool> RequestManager::RequestJoinRoom(const std::string& roomid)
{
	json js;
	js["command"] = LobbySubService_RequestJoinRoom;
	js["roomid"] = roomid;

	json js_resp;
	if (!co_await ConnectManager::Instance()->SocialRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "LobbySubService_RequestJoinRoom Request fail! reason:" << reason << '\n';
		co_return false;
	}

	co_return true;
}

Task<bool> RequestManager::RequestLeaveRoom()
{
	json js;
	js["command"] = LobbySubService_RequestLeaveRoom;

	json js_resp;
	if (!co_await ConnectManager::Instance()->SocialRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "LobbySubService_RequestLeaveRoom Request fail! reason:" << reason << '\n';
		co_return false;
	}

	co_return true;
}

Task<bool> RequestManager::RequestChangeReadyStatus(bool isready)
{
	json js;
	js["command"] = LobbySubService_RequestChangeReadyStatus;
	js["status"] = isready ? 1 : -1;

	json js_resp;
	if (!co_await ConnectManager::Instance()->SocialRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "LobbySubService_RequestJoinRoom Request fail! reason:" << reason << '\n';
		co_return false;
	}

	co_return true;
}

Task<bool> RequestManager::RequestStartGame(json& response)
{
	json js;
	js["command"] = LobbySubService_RequestStartGame;

	json js_resp;
	if (!co_await ConnectManager::Instance()->SocialRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "LobbySubService_RequestStartGame Request fail! reason:" << reason << '\n';
		co_return false;
	}

	response = js_resp;
	co_return true;
}

Task<bool> RequestManager::RequestLeaveGame()
{
	json js;
	js["command"] = GameServiceCommand::GameService_LeaveGame;

	json js_resp;
	if (!co_await ConnectManager::Instance()->GameRequest(js, js_resp))
		co_return false;

	if (!js_resp.contains("result") || !js_resp["result"].is_number_integer())
		co_return false;

	int result = js_resp["result"];
	if (result < 0)
	{
		std::string reason = js_resp.value("reason", "");
		std::cout << "GameService_LeaveGame Request fail! reason:" << reason << '\n';
		co_return false;
	}

	co_return true;
}

