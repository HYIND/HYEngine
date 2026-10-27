#pragma once

// 引擎的 UI 事件总线 —— 这是 UI 交互对外的唯一接口面。
//
// 一条命令的完整旅程：
//   .ui 里写 "onClick": { "command": "lobby.join" }
//     -> 用户点了按钮
//     -> RendererUI 在渲染线程把事件攒进自己的 m_events
//     -> Renderer::WaitImage 每帧调 PollEvents() 取出，经 sink 送给 Post()
//     -> 场景线程调用 Pump()，拿着 event.command 去每个模块名下找订阅者并调用
//
// handler 在写 World 的那个线程上跑（放场景线程 / 编辑器主线程），所以里面可以
// 直接读写组件，不需要加锁。
//
// 一条 command 可以被多个模块订阅，派发时全部调用。典型用法：大厅模块处理
// "lobby.join" 的业务，统计/埋点模块同时订阅它做记录，两者互不知道。
//
// 表只有一张：moduleId -> { command -> handler }。
//   Register / UnregisterByModule 都按 moduleId 定位，模块卸载就是删一个 entry。
//   Pump 要按 command 找人，所以在模块表上逐个 entry 做一次哈希查找，代价是
//   O(事件数 × 模块数)，模块数是个位到几十的量级，可以接受；换来的是没有第二份
//   需要同步维护的索引。
//
// 调用约定：
//   Post           任意线程可调（内部加锁），渲染线程从这里进来。
//   Pump           只能在写 World 的线程调（场景线程 / 编辑器主线程）。
//   Register       在模块加载时调；与 Pump 互斥保护，handler 里再注册也安全。
//   UnregisterByModule  模块卸载前必须调，否则表里会留下指向已卸载 DLL 的 handler。

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "UI/UiTypes.h"

class World;

namespace UI
{
	// 一条命令的处理者。event 是纯数据，world 是当前场景，仅在这次调用期间有效。
	using CommandHandler = std::function<void(const UiEvent&, World&)>;

	// 已注册命令的只读描述，给编辑器校验 .ui 用。一条 command 有多个订阅者时出现多条。
	struct CommandInfo
	{
		std::string command;
		std::string moduleId;
	};

	// 一次 Pump 的结果。没人认领的命令说明 .ui 写错了或模块没注册，值得报出来。
	struct PumpReport
	{
		// 至少有一个订阅者并已派发的事件数（多订阅者只算一次）。
		int dispatched = 0;
		// 没有任何订阅者的事件数。
		int unclaimed = 0;
		std::vector<std::string> unclaimedCommands;
	};

	class UICommandBus
	{
	public:
		// 订阅一条 command。同一 command 可以被多个模块订阅；同一模块重复订阅同一条
		// command 视为配置错误：返回 false，保留先注册的那个。
		bool Register(const std::string& moduleId, const std::string& command, CommandHandler handler);

		// 模块卸载时整块删掉。
		void UnregisterByModule(const std::string& moduleId);

		std::vector<CommandInfo> GetCommands() const;

		// 生产侧（渲染线程）。
		void Post(UiEvent event);
		void Post(const std::vector<UiEvent>& events);

		// 消费侧（场景线程）。取出全部待处理事件并派发给全部订阅者。
		PumpReport Pump(World& world);

		// 还没被取走的事件数（诊断用）。
		std::size_t PendingCount() const;

	private:
		struct Subscription
		{
			std::string command;
			CommandHandler handler;
		};

		struct Entry
		{
			std::string moduleId;
			std::unordered_map<std::string, Subscription> commands;
		};
	private:
		static constexpr std::size_t MaxPendingEvent = 8192;

		mutable CriticalSectionLock m_mutex;
		std::vector<UiEvent> m_pending;

		// moduleId -> Entry
		std::unordered_map<std::string, Entry> m_table;
	};
}
