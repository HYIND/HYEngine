#pragma once

#include <string>

class World;
namespace UI { class MessageBus; struct UiEvent; }
namespace NComponentCodec { struct ModuleHostServices; }

class UiRouter
{
public:
	enum class Screen { Main, Hall, Room, Option };

	void Register(UI::MessageBus& bus, const std::string& moduleId);// 把命令登记到引擎总线上。

	void SetHostServices(const NComponentCodec::ModuleHostServices* services) { m_services = services; }

	// 界面名
	static const char* ViewName(Screen screen);

private:
	void Handle(const UI::UiEvent& event, World& world);

	void RequestQuit();
	void RequestChangeScene(const char* sceneAsset);

	// 只开一个界面：其它三个关掉。关掉的视图在渲染收集阶段就被跳过，不遍历节点树。
	void Show(World& world, Screen screen);
	void EnterHall(World& world);
	void EnterRoom(World& world);

	void SyncOptionValues(World& world);
	bool ApplyAndSaveSettings();

	const NComponentCodec::ModuleHostServices* m_services = nullptr;

	Screen m_current = Screen::Main;
	Screen m_previous = Screen::Main;   // 选项关掉时回哪里

	int m_selectedRoom = -1;            // -1 = 还没选
};
