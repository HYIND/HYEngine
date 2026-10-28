#include "UiRouter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ECSCore/World.h"
#include "SceneCodec/ModuleHostServices.h"
#include "UI/MessageBus.h"
#include "UI/UiDom.h"
#include "UI/UiTypes.h"
#include "UserSettings.h"

namespace
{
	// 场景里配的四个视图名，和 testui.scene 的 engine.uiview 一一对应。
	const char* const kScreenViews[] = { "Main", "Hall", "Room", "Option" };

	// 所有命令串只在这里出现一次，加一条屏只改这张表 + .ui 两处。
	const char* const kCommands[] = {
		"scene.hall", "scene.exit_hall", "scene.exit_room",
		"scene.option", "scene.exit_option",
		"scene.local_game", "scene.map_edit", "scene.quit",
		"hall.refresh", "hall.join_room", "hall.create_room",
		"hall.select_room", "hall.enter_room", "hall.send_chat",
		"room.start_game", "room.send_chat",
		"option.fps", "option.master_volume", "option.music_volume", "option.sound_volume",
		"gaming.local_return", "gaming.online_return"
	};

	UI::Doc FindScreen(World& world, UiRouter::Screen screen)
	{
		const char* name = UiRouter::ViewName(screen);
		std::string error;
		UI::Doc doc = UI::FindDoc(world, name, &error);
		if (!doc.Valid())
			std::printf("[ui] 找不到界面 %s：%s\n", name, error.c_str());
		return doc;
	}

	std::string Pad2(int value)
	{
		char buffer[8] = {};
		std::snprintf(buffer, sizeof(buffer), "%02d", value);
		return buffer;
	}

	// 填充一个 scrolllist：行文本 + 行 id。id 为空的行双击时 itemId 也是空，用 value（行下标）。
	void FillList(UI::Doc& doc, const char* nodeName, const std::vector<std::string>& rows, const char* idPrefix)
	{
		UI::Element list = doc.Get(nodeName);
		if (!list.Valid())
			return;

		std::vector<UI::UiListItem> items;
		items.reserve(rows.size());
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			UI::UiListItem item;
			item.text = rows[i];
			item.id = std::string(idPrefix) + Pad2(static_cast<int>(i) + 1);
			items.push_back(std::move(item));
		}

		list.SetItems(std::move(items));
		list.SetValue(-1.0f);   // 新列表没有选中行（-1 和任何行下标都不相等）
	}

	void AppendChat(UI::Doc& doc, const std::string& line)
	{
		UI::Element history = doc.Get("ChatHistory");
		if (history.Valid())
			history.AppendLine(line);
	}

	// 发一条：读输入框的编辑缓冲（不是 label，textinput 的真实内容在缓冲里，
	// 而且此刻大概率还没失焦、onChange 还没发过），追加到历史，然后清空。
	void SendChat(UI::Doc& doc, const char* speaker)
	{
		UI::Element input = doc.Get("ChatInput");
		UI::Element history = doc.Get("ChatHistory");
		if (!input.Valid() || !history.Valid())
			return;

		const std::string text = input.InputText();
		if (text.empty())
			return;

		history.AppendLine(std::string(speaker) + "：" + text);
		input.SetInputText(std::string());
	}

	void FillRooms(UI::Doc& hall)
	{
		// 假数据：真实房间列表要等网络层搬进模块后接上来。
		const int players[5] = { 1, 3, 0, 8, 2 };
		std::vector<std::string> rows;
		rows.reserve(5);
		for (int i = 0; i < 5; ++i)
			rows.push_back("房间 " + Pad2(i + 1) + "    " + std::to_string(players[i]) + "/8");

		FillList(hall, "RoomList", rows, "room_");
	}

	void FillUsers(UI::Doc& doc, const char* nodeName)
	{
		static const char* const names[] = { "玩家一", "玩家二", "玩家三", "玩家四", "玩家五", "玩家六" };
		std::vector<std::string> rows(names, names + 6);
		FillList(doc, nodeName, rows, "user_");
	}
}

const char* UiRouter::ViewName(Screen screen)
{
	switch (screen)
	{
	case Screen::Main:   return "Main";
	case Screen::Hall:   return "Hall";
	case Screen::Room:   return "Room";
	case Screen::Option: return "Option";
	}
	return "";
}

void UiRouter::Register(UI::MessageBus& bus, const std::string& moduleId)
{
	for (const char* command : kCommands)
	{
		bus.RegisterUiCommand(moduleId, command,
			[this](const UI::UiEvent& event, World& world) { Handle(event, world); });
	}
}

void UiRouter::Show(World& world, Screen screen)
{
	if (screen == Screen::Option)
		SyncOptionValues(world);

	for (const char* name : kScreenViews)
	{
		UI::Doc doc = UI::FindDoc(world, name);
		if (doc.Valid())
			doc.SetEnabled(std::strcmp(name, ViewName(screen)) == 0);
	}

	if (screen != m_current)
		m_previous = m_current;
	m_current = screen;
}

void UiRouter::SyncOptionValues(World& world)
{
	UI::Doc option = FindScreen(world, Screen::Option);
	if (!option.Valid())
		return;

	const UserSettings& settings = GetUserSettings();
	option.Get("FpsCombo").SetValue(static_cast<float>(settings.TargetFps()));
	option.Get("MasterVolume").SetValue(static_cast<float>(settings.MasterVolume()));
	option.Get("MusicVolume").SetValue(static_cast<float>(settings.MusicVolume()));
	option.Get("SoundVolume").SetValue(static_cast<float>(settings.SfxVolume()));
}

bool UiRouter::ApplyAndSaveSettings()
{
	UserSettings& settings = GetUserSettings();
	if (m_services)
		settings.Apply(*m_services);
	return settings.Save();
}

void UiRouter::RequestQuit()
{
	if (!m_services || !m_services->requestQuit)
		return;
	m_services->requestQuit(m_services->serviceOwner);
}

void UiRouter::RequestChangeScene(const char* sceneAsset)
{
	if (!m_services || !m_services->requestSceneChange)
		return;
	if (sceneAsset)
		m_services->requestSceneChange(m_services->serviceOwner, sceneAsset);
}

void UiRouter::EnterHall(World& world)
{
	Show(world, Screen::Hall);

	UI::Doc hall = FindScreen(world, Screen::Hall);
	if (!hall.Valid())
		return;

	FillRooms(hall);
	FillUsers(hall, "UserList");
	hall.Get("ChatHistory").ClearLines();
	AppendChat(hall, "系统：欢迎来到大厅");
}

void UiRouter::EnterRoom(World& world)
{
	if (m_selectedRoom < 0)
	{
		// 没选房间就进来了：给个提示，留在原界面。
		UI::Doc hall = FindScreen(world, Screen::Hall);
		if (hall.Valid())
			AppendChat(hall, "系统：先选一个房间再进");
		return;
	}

	Show(world, Screen::Room);

	UI::Doc room = FindScreen(world, Screen::Room);
	if (!room.Valid())
		return;

	room.Get("RoomTitle").SetText("房间 " + Pad2(m_selectedRoom + 1));
	FillUsers(room, "PlayerList");
	room.Get("ChatHistory").ClearLines();
	AppendChat(room, "系统：你已进入房间 " + Pad2(m_selectedRoom + 1));
}

void UiRouter::Handle(const UI::UiEvent& event, World& world)
{
	const std::string& command = event.command;

	if (command == "scene.hall") { EnterHall(world); return; }
	if (command == "scene.exit_room") { EnterHall(world); return; }
	if (command == "scene.exit_hall") { Show(world, Screen::Main); return; }
	if (command == "scene.option") { Show(world, Screen::Option); return; }
	if (command == "scene.exit_option") { Show(world, m_previous == Screen::Option ? Screen::Main : m_previous); return; }

	if (command == "scene.local_game")
	{
		RequestChangeScene("Untitled222.scene");
		return;
	}

	if (command == "scene.map_edit")
		return;

	if (command == "scene.quit")
	{
		RequestQuit();
		return;
	}

	if (command == "hall.refresh" || command == "hall.join_room" || command == "hall.create_room")
	{
		UI::Doc hall = FindScreen(world, Screen::Hall);
		if (hall.Valid())
			FillRooms(hall);   // 还没有服务端，刷新/加入/创建都只是重填一遍
		return;
	}

	if (command == "hall.select_room")
	{
		UI::Doc hall = FindScreen(world, Screen::Hall);
		if (!hall.Valid())
			return;

		m_selectedRoom = static_cast<int>(event.value);   // 行事件把行下标放在 value
		hall.Get("RoomList").SetValue(event.value);       // 高亮读的就是 value
		return;
	}

	if (command == "hall.enter_room")
	{
		if (event.value >= 0.0f)
			m_selectedRoom = static_cast<int>(event.value);
		EnterRoom(world);
		return;
	}

	if (command == "hall.send_chat")
	{
		UI::Doc hall = FindScreen(world, Screen::Hall);
		if (hall.Valid())
			SendChat(hall, "我");
		return;
	}

	// ---- 房间 ----
	if (command == "room.send_chat")
	{
		UI::Doc room = FindScreen(world, Screen::Room);
		if (room.Valid())
			SendChat(room, "我");
		return;
	}

	if (command == "room.start_game")
	{
		// 进游戏世界是下一步：GameLocal 屏和世界加载都还没接。
		UI::Doc room = FindScreen(world, Screen::Room);
		if (room.Valid())
			AppendChat(room, "系统：开局功能还没接");
		return;
	}

	if (command == "option.fps")
	{
		GetUserSettings().SetTargetFps(static_cast<int>(event.value));
		ApplyAndSaveSettings();
		return;
	}
	if (command == "option.master_volume")
	{
		GetUserSettings().SetMasterVolume(static_cast<int>(event.value));
		ApplyAndSaveSettings();
		return;
	}
	if (command == "option.music_volume")
	{
		GetUserSettings().SetMusicVolume(static_cast<int>(event.value));
		ApplyAndSaveSettings();
		return;
	}
	if (command == "option.sound_volume")
	{
		GetUserSettings().SetSfxVolume(static_cast<int>(event.value));
		ApplyAndSaveSettings();
		return;
	}

	if (command == "gaming.local_return")
	{
		RequestChangeScene("testui.scene");
		return;
	}
}
