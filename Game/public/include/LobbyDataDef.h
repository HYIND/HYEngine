#pragma once

#include <iostream>
#include "SafeStl.h"
#include "Coroutine.h"
#include "stdafx.h"

struct LobbyUser
{
	std::string token;
	std::string name;
};

enum class RoomStatus
{
	IN_GAME, // 游戏中
	READY    // 准备中
};

enum class MemberStatus
{
	NO_READY, // 未准备
	READY     // 准备中
};

struct RoomMemeber
{
	std::string name;
	MemberStatus status;
};

struct Room
{
	std::string room_id;   // 房间ID
	std::string room_name; // 房间名称
	std::string room_host_token;

	int player_count; // 房间人数
	int max_players;  // 房间最大人数

	RoomStatus status;

	SafeMap<std::string, std::shared_ptr<RoomMemeber>, CoroCriticalSectionLock> members; // token->Info

	json ToJson()
	{
		json j;
		j["room_id"] = room_id;
		j["room_name"] = room_name;
		j["room_host_token"] = room_host_token;
		j["player_count"] = player_count;
		j["max_players"] = max_players;
		j["status"] = static_cast<int>(status);

		// 序列化成员
		json members_json = json::array();
		members.EnsureCall(
			[&](std::map<std::string, std::shared_ptr<RoomMemeber>>& members_map) -> void
			{
				for (const auto& [token, member] : members_map)
				{
					if (member)
					{
						json member_json = {
							{"token", token},
							{"name", member->name},
							{"status", static_cast<int>(member->status)} };

						members_json.emplace_back(member_json);
					}
				}
			});
		j["members"] = members_json;

		return j;
	}

	// 从JSON更新
	bool UpdateFromJson(const json& j)
	{
		try
		{
			if (j.contains("room_id") && j["room_id"].is_string())
			{
				room_id = j["room_id"].get<std::string>();
			}

			if (j.contains("room_name") && j["room_name"].is_string())
			{
				room_name = j["room_name"].get<std::string>();
			}

			if (j.contains("room_host_token") && j["room_host_token"].is_string())
			{
				room_host_token = j["room_host_token"].get<std::string>();
			}

			if (j.contains("player_count") && j["player_count"].is_number())
			{
				player_count = j["player_count"].get<int>();
			}

			if (j.contains("max_players") && j["max_players"].is_number())
			{
				max_players = j["max_players"].get<int>();
			}

			if (j.contains("status") && j["status"].is_number())
			{
				status = static_cast<RoomStatus>(j["status"].get<int>());
			}

			if (j.contains("members") && j["members"].is_array())
			{
				members.Clear();
				auto members_json = j["members"];
				for (int i = 0; i < members_json.size(); i++)
				{
					json member_json = members_json.at(i);
					if (member_json.is_object() &&
						member_json.contains("token") && member_json["token"].is_string() &&
						member_json.contains("name") && member_json["name"].is_string() &&
						member_json.contains("status") && member_json["status"].is_number_integer())
					{
						std::string token = member_json.value("token", "");
						if (!token.empty())
						{
							auto member = std::make_shared<RoomMemeber>();
							member->name = member_json.value("name", "");
							member->status = (MemberStatus)member_json.value("status", 0);
							members[token] = member;
						}
					}
				}
			}

			return true;
		}
		catch (const std::exception& e)
		{
			return false;
		}
	}
};