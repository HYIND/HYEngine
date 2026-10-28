#pragma once

// 引擎消息总线

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "UI/Message.h"
#include "UI/UiTypes.h"

class World;

namespace UI
{
	using MessageHandler = std::function<void(const Message&, World&)>;
	using UiEventHandler = std::function<void(const UiEvent&, World&)>;

	struct CommandInfo
	{
		std::string command;
		std::string moduleId;
	};

	// 一次 Pump 的结果
	struct PumpReport
	{
		int dispatched = 0;	// 至少有一个订阅者并已派发的消息数（多订阅者只算一次）。
		int unclaimed = 0;	// 没有任何订阅者的消息数。
		std::vector<std::string> unclaimedCommands;
	};

	class MessageBus
	{
	public:
		bool Register(const std::string& moduleId, const std::string& topic, MessageHandler handler);
		bool RegisterUiCommand(const std::string& moduleId, const std::string& command, UiEventHandler handler);

		// 模块卸载时整块删掉。
		void UnregisterByModule(const std::string& moduleId);

		// 生产侧
		void Post(Message message);
		void Post(const UiEvent& event);
		void Post(const std::vector<UiEvent>& events);
		
		PumpReport Pump(World& world);	// 消费侧，取出全部待处理消息并派发给全部订阅者。
		std::size_t Discard();			// 取出来后丢掉消息
		
		std::size_t PendingCount() const;// 还没被取走的消息数（诊断用）。

	private:
		struct Subscription
		{
			std::string topic;
			MessageHandler handler;
		};

		struct Entry
		{
			std::string moduleId;
			std::unordered_map<std::string, Subscription> topics;
		};

		void PostBatch(std::vector<Message>& batch);

	private:
		static constexpr std::size_t MaxPendingEvent = 8192;

		mutable CriticalSectionLock m_mutex;
		std::vector<Message> m_pending;

		// moduleId -> Entry
		std::unordered_map<std::string, Entry> m_table;
	};
}
