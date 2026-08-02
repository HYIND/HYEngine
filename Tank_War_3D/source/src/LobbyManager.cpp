#include "Manager/LobbyManager.h"
#include "Manager/UserInfoManager.h"
#include "Manager/RequestManager.h"
#include "Manager/MsgManager.h"
#include "Manager/ConnectManager.h"
#include "Manager/RenderContextManager.h"
#include "Scene.h"
#include "command.h"
#include "ServiceRegistryData.h"

using namespace LobbySubServiceCommand;

LobbyManager* LobbyManager::Instance()
{
	static LobbyManager* instance = new LobbyManager();
	return instance;
}

LobbyManager::LobbyManager() {}
LobbyManager::~LobbyManager() {}

void LobbyManager::ProcessLobbyUserData(const json& js)
{
	if (!js.contains("users") || !js["users"].is_array())
		return;
	json js_users = js["users"];

	std::vector<LobbyUser> users;
	for (int i = 0; i < js_users.size(); i++)
	{
		json js_user = js["users"].at(i);
		if (!js_user.contains("token") || !js_user.contains("name"))
			continue;
		std::string token = js_user.value("token", "");
		std::string name = js_user.value("name", "");
		if (token.empty() || name.empty())
			continue;

		if (UserInfoManager::Instance()->isMyToken(token))
			users.insert(users.begin(), (LobbyUser{ .token = token,.name = name }));
		else
			users.emplace_back(LobbyUser{ .token = token,.name = name });

	}

	(int)SendMessage(_Scene::SHall->Hall_user_list, LB_RESETCONTENT, 0, 0);
	for (auto& user : users)
	{
		std::string& name = user.name;
		if (UserInfoManager::Instance()->isMyToken(user.token))
			name += "(您)";
		(int)SendMessage(_Scene::SHall->Hall_user_list, LB_ADDSTRING, 0, (LPARAM) & (Tool::UTF8ToWString(user.name)[0]));
	}
}

void LobbyManager::ProcessLobbyRoomData(const json& js)
{
	if (!js.contains("rooms") || !js["rooms"].is_array())
		return;
	json js_rooms = js["rooms"];

	LockGuard guard(_curroomsmutex);
	for (auto room : _currooms)
	{
		if (room)
			delete room;
	}
	_currooms.clear();


	for (int i = 0; i < js_rooms.size(); i++)
	{
		json js_room = js["rooms"].at(i);
		if (!js_room.contains("roomid")
			|| !js_room.contains("roomname")
			|| !js_room.contains("playercount")
			|| !js_room.contains("maxplayer")
			|| !js_room.contains("roomstatus")
			)
			continue;
		std::string room_id = js_room.value("roomid", "");
		std::string room_name = js_room.value("roomname", "");
		int playercount = js_room.value("playercount", 0);
		int maxplayer = js_room.value("maxplayer", 0);
		RoomStatus room_status = (RoomStatus)js_room.value("roomstatus", 0);
		if (room_id.empty())
			continue;

		_currooms.emplace_back(new Room{
			.room_id = room_id ,
			.room_name = room_name,
			.player_count = playercount,
			.max_players = maxplayer,
			.status = room_status
			});
	}

	{
		int currentSel = (int)SendMessage(_Scene::SHall->Hall_room_list, LB_GETCURSEL, 0, 0);

		(int)SendMessage(_Scene::SHall->Hall_room_list, LB_RESETCONTENT, 0, 0);

		for (auto room : _currooms)
		{
			string format_roomname = room->status == RoomStatus::IN_GAME ?
				std::format(" {} [{}/{}] (游戏中)", room->room_name, room->player_count, room->max_players) :
				std::format(" {} [{}/{}]", room->room_name, room->player_count, room->max_players);

			int index = (int)SendMessage(_Scene::SHall->Hall_room_list, LB_ADDSTRING, 0, (LPARAM) & (Tool::UTF8ToWString(format_roomname))[0]);
			SendMessage(_Scene::SHall->Hall_room_list, LB_SETITEMDATA, index, (LPARAM)room);  // 存储房间指针
		}

		if (currentSel != LB_ERR) {
			int itemCount = (int)SendMessage(_Scene::SHall->Hall_room_list, LB_GETCOUNT, 0, 0);
			if (currentSel < itemCount) {
				SendMessage(_Scene::SHall->Hall_room_list, LB_SETCURSEL, currentSel, 0);
			}
		}
	}
}

void LobbyManager::ProcessHallMsg(const json& js)
{
	if (!js.contains("srctoken")
		|| !js.contains("name")
		|| !js.contains("msg"))
		return;

	std::string srctoken = js.value("srctoken", "");
	std::string name = js.value("name", "");
	std::string msg = js.value("msg", "");
	std::string timestr = Tool::GetFormatSecondStr(Tool::GetTimestampSecond());


	if (srctoken.empty() || name.empty() || msg.empty())
		return;

	std::string fomat_message = std::format("[{}]  [{}] \r\n{}\r\n", name, timestr, msg);

	SendMessage(_Scene::SHall->edit_hall, EM_REPLACESEL, FALSE, (LPARAM) & (Tool::UTF8ToWString(fomat_message)[0]));
}

void LobbyManager::ProcessRoomMsg(const json& js)
{
	if (!js.contains("srctoken")
		|| !js.contains("name")
		|| !js.contains("msg"))
		return;

	std::string srctoken = js.value("srctoken", "");
	std::string name = js.value("name", "");
	std::string msg = js.value("msg", "");
	std::string timestr = Tool::GetFormatSecondStr(Tool::GetTimestampSecond());

	if (srctoken.empty() || name.empty() || msg.empty())
		return;

	std::string fomat_message = std::format("[{}]  {} :\r\n{}\r\n", name, timestr, msg);

	SendMessage(_Scene::SRoom->edit_room, EM_REPLACESEL, FALSE, (LPARAM) & (Tool::UTF8ToWString(fomat_message)[0]));
}

void LobbyManager::ProcessPrivateMsg(const json& js)
{
	if (!js.contains("srctoken")
		|| !js.contains("goaltoken")
		|| !js.contains("name")
		|| !js.contains("msg"))
		return;

	std::string srctoken = js.value("srctoken", "");
	std::string goaltoken = js.value("goaltoken", "");
	std::string name = js.value("name", "");
	std::string msg = js.value("msg", "");
	std::string timestr = Tool::GetFormatSecondStr(Tool::GetTimestampSecond());

	if (srctoken.empty() || goaltoken.empty() || name.empty() || msg.empty())
		return;

	if (!UserInfoManager::Instance()->isMyToken(goaltoken))
		return;

	std::string fomat_message = std::format("{}  {} :\r\n{}\r\n", name, timestr, msg);
}

void LobbyManager::ProcessSendMsgReceipt(const json& js)
{
	int command = js.value("command", 0);
	if (command == LobbySubService_SendHallMsgRes)
	{
		int result = js.value("result", -1);
		if (result < 1)
		{
			std::string reason = js.value("reason", "");
			std::cout << "LobbySubService_SendHallMsg Request fail! reason:" << reason << '\n';
		}
	}
	if (command == LobbySubService_SendRoomMsgRes)
	{
		int result = js.value("result", -1);
		if (result < 1)
		{
			std::string reason = js.value("reason", "");
			std::cout << "LobbySubService_SendRoomMsg Request fail! reason:" << reason << '\n';
		}
	}
	if (command == LobbySubService_SendPrivateMsgRes)
	{
		int result = js.value("result", -1);
		if (result < 1)
		{
			std::string reason = js.value("reason", "");
			std::cout << "LobbySubService_SendPrivateMsg Request fail! reason:" << reason << '\n';
		}
	}
}

void LobbyManager::ProcessRoomInfo(const json& js)
{
	int result = js.value("result", -1);
	if (result < 1)
	{
		std::string reason = js.value("reason", "");
		std::cout << "ProcessRoomInfo Request fail! reason:" << reason << '\n';
		return;
	}

	if (!js.contains("roominfo") || !js["roominfo"].is_object())
	{
		std::cout << "ProcessRoomInfo Request fail with null array!\n";
		return;
	}

	auto info = _curroominfo;
	if (!info)
		return;

	info->UpdateFromJson(js["roominfo"]);

	(int)SendMessage(_Scene::SRoom->Room_user_list, LB_RESETCONTENT, 0, 0);

	info->members.EnsureCall(
		[&](std::map<std::string, std::shared_ptr<RoomMemeber>>& members_map) -> void
		{
			for (auto& [token, memberinfo] : members_map)
			{
				std::string name = memberinfo->name;
				if (UserInfoManager::Instance()->isMyToken(token))
				{
					name += "(您)";
					LOBBYMANAGER->SetIsHost(UserInfoManager::Instance()->isMyToken(info->room_host_token));
					_Scene::SRoom->SetScene(LOBBYMANAGER->IsHost(), memberinfo->status == MemberStatus::READY);
				}
				if (token == info->room_host_token)
				{
					string format_membername = std::format("{} {}", name, "[房主]");
					int index = (int)SendMessage(_Scene::SRoom->Room_user_list, LB_ADDSTRING, 0, (LPARAM) & (Tool::UTF8ToWString(format_membername))[0]);
				}
				else
				{
					string format_membername = std::format("{} {}", name, memberinfo->status == MemberStatus::READY ? "[已准备]" : "");
					int index = (int)SendMessage(_Scene::SRoom->Room_user_list, LB_ADDSTRING, 0, (LPARAM) & (Tool::UTF8ToWString(format_membername))[0]);
				}
			}
		});
}

Task<void> LobbyManager::ProcessStartGameRes(const json& js)
{
	if (!js.contains("command") || !js["command"].is_number_integer())
		co_return;

	if (!js.contains("result") || !js["result"].is_number_integer())
		co_return;

	int command = js["command"];
	int result = js["result"];

	if (command != LobbySubService_RequestStartGameRes)
		co_return;
	if (result < 0)
		co_return;

	if (!js.contains("gameendpoint") || !js["gameendpoint"].is_object())
		co_return;

	json js_endpoint = js["gameendpoint"];
	if (!js_endpoint.contains("ip") ||
		!js_endpoint["ip"].is_string() ||
		!js_endpoint.contains("port") ||
		!js_endpoint["port"].is_number_integer())
		co_return;

	NetworkEndpoint endpoint;
	endpoint.update_from_json(js_endpoint);

	if (!js.contains("gameid") ||
		!js["gameid"].is_string())
		co_return;

	std::string gameid = js.value("gameid", "");
	UserInfoManager::Instance()->setGameId(gameid);

	if (!co_await CONNECTMANAGER->LoginGameSeervice(endpoint))
		co_return;

	json req, resp;
	req["command"] = GameServiceCommand::GameService_JoinGame;
	req["playerid"] = UserInfoManager::Instance()->usertoken();
	req["gameid"] = gameid;
	if (!co_await CONNECTMANAGER->GameRequest(req, resp))
		co_return;

	if (resp.contains("result") && resp["result"].is_number_integer())
	{
		int result = resp["result"];
		if (result < 0)
		{
			std::string reason = resp.value("reason", "");
			std::cout << "GameService_JoinGame Request fail! reason:" << reason << '\n';
			co_return;
		}
	}
	else
		co_return;

	SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, START, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
}

std::shared_ptr<Room> LobbyManager::GetSelectRoom()
{
	LockGuard guard(_curroomsmutex);
	int index = SendMessage(_Scene::SHall->Hall_room_list, LB_GETCURSEL, 0, 0);
	if (index != LB_ERR && index < _currooms.size())
	{
		Room* room = (Room*)SendMessage(_Scene::SHall->Hall_room_list, LB_GETITEMDATA, index, 0);
		return std::make_shared<Room>(*room);
	}
	return nullptr;
}

Task<bool> LobbyManager::TryJoinRoom(std::shared_ptr<Room> room)
{
	if (!room)
		co_return false;

	bool result = co_await REQUESTMANAGER->RequestJoinRoom(room->room_id);
	if (result)
	{
		SetIsHost(false);
		_curroominfo = std::make_shared<Room>();
	}
	co_return result;
}

Task<bool> LobbyManager::TryCreateRoom()
{
	bool result = co_await REQUESTMANAGER->RequestCreateRoom();
	if (result)
	{
		SetIsHost(true);
		_curroominfo = std::make_shared<Room>();
	}
	co_return result;
}

Task<bool> LobbyManager::TryLeaveRoom()
{
	bool result = co_await REQUESTMANAGER->RequestLeaveRoom();
	if (result)
		_curroominfo.reset();
	co_return result;
}

Task<bool> LobbyManager::TryChangeReadyStatus(bool isready)
{
	co_return co_await REQUESTMANAGER->RequestChangeReadyStatus(isready);
}

Task<bool> LobbyManager::TryStartGame()
{
	json resp;
	if (co_await REQUESTMANAGER->RequestStartGame(resp))
	{
		co_await ProcessStartGameRes(resp);
		co_return true;
	}
	co_return false;
}
