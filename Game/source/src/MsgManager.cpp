#include "Manager/MsgManager.h"
#include "Manager/LobbyManager.h"
#include "Manager/GameWorldManager.h"
#include "command.h"

using namespace LobbySubServiceCommand;
using namespace GameServiceCommand;

MsgManager* MsgManager::Instance()
{
	static MsgManager* instance = new MsgManager();
	return instance;
}

Task<void> MsgManager::ProcessMsg(const json& js)
{
	if (!js.contains("command"))
	{
		std::cerr << "ProcessMsg recv json without command!,data: \n" << js.dump() << '\n';
	}
	int command = js.value("command", 0);

	switch (command) {
	case LobbySubService_RequestLobbyUserRes:
		LOBBYMANAGER->ProcessLobbyUserData(js);
		break;

	case LobbySubService_RequestLobbyRoomRes:
		LOBBYMANAGER->ProcessLobbyRoomData(js);
		break;

	case LobbySubService_HallMsgBroadcast:
		LOBBYMANAGER->ProcessHallMsg(js);
		break;

	case LobbySubService_RoomMsgBroadcast:
		LOBBYMANAGER->ProcessRoomMsg(js);
		break;

	case LobbySubService_PrivateMsgForward:
		LOBBYMANAGER->ProcessPrivateMsg(js);
		break;

	case LobbySubService_SendHallMsgRes:
	case LobbySubService_SendRoomMsgRes:
	case LobbySubService_SendPrivateMsgRes:
		LOBBYMANAGER->ProcessSendMsgReceipt(js);
		break;

	case LobbySubService_RequestRoomInfoRes:
		LOBBYMANAGER->ProcessRoomInfo(js);
		break;

	case LobbySubService_RequestStartGameRes:
		co_await LOBBYMANAGER->ProcessStartGameRes(js);
		break;

	case GameService_BroadCastGameState:
		GameWorldManager::Instance()->SyncFromServerState(js);
		break;

	case GameService_SyncGameEvent:
		GameWorldManager::Instance()->SyncFromServerEvent(js);
		break;

	case GameService_EliminateInfo:
		GameWorldManager::Instance()->ProcessEliminateInfo(js);
		break;

	case GameService_GameOverInfo:
		GameWorldManager::Instance()->ProcessGameOver(js);
		break;

	default:
		break;
	}
	co_return;
}

MsgManager::MsgManager() {}

MsgManager::~MsgManager() {}