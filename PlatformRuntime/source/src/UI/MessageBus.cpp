#include "UI/MessageBus.h"

#include "ECSCore/World.h"

#include <algorithm>
#include <utility>

namespace UI
{
	bool MessageBus::Register(const std::string& moduleId, const std::string& topic, MessageHandler handler)
	{
		if (moduleId.empty() || topic.empty() || !handler)
			return false;

		std::lock_guard<CriticalSectionLock> lock(m_mutex);

		const auto moduleIt = m_table.find(moduleId);
		if (moduleIt == m_table.end())
		{
			Entry entry;
			entry.moduleId = moduleId;
			entry.topics.emplace(topic, Subscription{ topic, std::move(handler) });
			m_table.emplace(moduleId, std::move(entry));
			return true;
		}

		return moduleIt->second.topics.emplace(topic, Subscription{ topic, std::move(handler) }).second;
	}

	bool MessageBus::RegisterUiCommand(const std::string& moduleId, const std::string& command, UiEventHandler handler)
	{
		if (!handler)
			return false;

		return Register(moduleId, command,
			[handler = std::move(handler)](const Message& message, World& world)
			{
				handler(ToUiEvent(message), world);
			});
	}

	void MessageBus::UnregisterByModule(const std::string& moduleId)
	{
		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		m_table.erase(moduleId);
	}

	void MessageBus::Post(Message message)
	{
		std::vector<Message> batch;
		batch.push_back(std::move(message));
		PostBatch(batch);
	}

	void MessageBus::Post(const UiEvent& event)
	{
		Post(FromUiEvent(event));
	}

	void MessageBus::Post(const std::vector<UiEvent>& events)
	{
		if (events.empty())
			return;

		// 只在渲染线程上做这次转换；入队一次锁。
		std::vector<Message> batch;
		batch.reserve(events.size());
		for (const auto& event : events)
			batch.push_back(FromUiEvent(event));
		PostBatch(batch);
	}

	void MessageBus::PostBatch(std::vector<Message>& batch)
	{
		if (batch.empty())
			return;

		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		for (auto& message : batch)
		{
			// 丢掉最旧的：宁可丢失一条陈旧的点击，也不要让队列吃满内存。
			if (m_pending.size() >= MaxPendingEvent)
				m_pending.erase(m_pending.begin());
			m_pending.push_back(std::move(message));
		}
	}

	PumpReport MessageBus::Pump(World& world)
	{
		std::vector<Message> batch;
		{
			// 只在换手的时候拿锁。锁本身是递归的，持锁调 handler 不会死锁，
			// 但那样会把渲染线程的 Post 卡在派发期间，所以还是换完就放。
			std::lock_guard<CriticalSectionLock> lock(m_mutex);
			batch.swap(m_pending);
		}

		PumpReport report;
		for (const auto& message : batch)
		{
			// 把这一条消息的全部订阅者先抄出来，然后放锁调用：handler 里可能又要
			// Register / UnregisterByModule，直接迭代 m_table 会踩到被改的容器
			// （递归锁救得了重入，救不了"边遍历边改"）。
			std::vector<std::pair<std::string, MessageHandler>> subscribers;
			{
				std::lock_guard<CriticalSectionLock> lock(m_mutex);
				for (const auto& pair : m_table)
				{
					const Entry& entry = pair.second;
					const auto found = entry.topics.find(message.topic);
					if (found != entry.topics.end())
						subscribers.emplace_back(entry.moduleId, found->second.handler);
				}
			}

			if (subscribers.empty())
			{
				++report.unclaimed;
				if (std::find(report.unclaimedCommands.begin(), report.unclaimedCommands.end(), message.topic)
					== report.unclaimedCommands.end())
				{
					report.unclaimedCommands.push_back(message.topic);
				}
				continue;
			}

			// unordered_map 的遍历顺序不保证，排序让同一条 topic 的派发顺序稳定可复现。
			std::sort(subscribers.begin(), subscribers.end(),
				[](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

			for (const auto& subscriber : subscribers)
			{
				if (auto& handle = subscriber.second)
					handle(message, world);
			}

			++report.dispatched;
		}
		return report;
	}

	std::size_t MessageBus::Discard()
	{
		std::vector<Message> dropped;
		{
			std::lock_guard<CriticalSectionLock> lock(m_mutex);
			dropped.swap(m_pending);
		}
		return dropped.size();
	}

	std::size_t MessageBus::PendingCount() const
	{
		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		return m_pending.size();
	}
}
