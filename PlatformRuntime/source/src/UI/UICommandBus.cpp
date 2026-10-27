#include "UI/UICommandBus.h"

#include "ECSCore/World.h"

#include <algorithm>
#include <utility>

namespace UI
{
	bool UICommandBus::Register(const std::string& moduleId, const std::string& command, CommandHandler handler)
	{
		if (moduleId.empty() || command.empty() || !handler)
			return false;

		std::lock_guard<CriticalSectionLock> lock(m_mutex);

		const auto moduleIt = m_table.find(moduleId);
		if (moduleIt == m_table.end())
		{
			Entry entry;
			entry.moduleId = moduleId;
			entry.commands.emplace(command, Subscription{ command, std::move(handler) });
			m_table.emplace(moduleId, std::move(entry));
			return true;
		}

		// 同一模块重复订阅同一条命令是配置错误，别悄悄叠一份。
		return moduleIt->second.commands.emplace(command, Subscription{ command, std::move(handler) }).second;
	}

	void UICommandBus::UnregisterByModule(const std::string& moduleId)
	{
		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		m_table.erase(moduleId);
	}

	std::vector<CommandInfo> UICommandBus::GetCommands() const
	{
		std::lock_guard<CriticalSectionLock> lock(m_mutex);

		std::vector<CommandInfo> commands;
		for (const auto& pair : m_table)
		{
			const Entry& entry = pair.second;
			for (const auto& subscription : entry.commands)
				commands.push_back(CommandInfo{ subscription.second.command, entry.moduleId });
		}
		return commands;
	}

	void UICommandBus::Post(UiEvent event)
	{
		std::vector<UiEvent> batch;
		batch.push_back(std::move(event));
		Post(batch);
	}

	void UICommandBus::Post(const std::vector<UiEvent>& events)
	{
		if (events.empty())
			return;

		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		for (const auto& event : events)
		{
			// 丢掉最旧的：宁可丢失一条陈旧的点击，也不要让队列吃满内存。
			if (m_pending.size() >= MaxPendingEvent)
				m_pending.erase(m_pending.begin());
			m_pending.push_back(event);
		}
	}

	PumpReport UICommandBus::Pump(World& world)
	{
		std::vector<UiEvent> batch;
		{
			// 只在换手的时候拿锁。锁本身是递归的，持锁调 handler 不会死锁，
			// 但那样会把渲染线程的 Post 卡在派发期间，所以还是换完就放。
			std::lock_guard<CriticalSectionLock> lock(m_mutex);
			batch.swap(m_pending);
		}

		PumpReport report;
		for (const auto& event : batch)
		{
			// 把这一条事件的全部订阅者先抄出来，然后放锁调用：handler 里可能又要
			// Register / UnregisterByModule，直接迭代 m_table 会踩到被改的容器
			// （递归锁救得了重入，救不了"边遍历边改"）。
			std::vector<std::pair<std::string, CommandHandler>> subscribers;
			{
				std::lock_guard<CriticalSectionLock> lock(m_mutex);
				for (const auto& pair : m_table)
				{
					const Entry& entry = pair.second;
					const auto found = entry.commands.find(event.command);
					if (found != entry.commands.end())
						subscribers.emplace_back(entry.moduleId, found->second.handler);
				}
			}

			if (subscribers.empty())
			{
				++report.unclaimed;
				if (std::find(report.unclaimedCommands.begin(), report.unclaimedCommands.end(), event.command)
					== report.unclaimedCommands.end())
				{
					report.unclaimedCommands.push_back(event.command);
				}
				continue;
			}

			// unordered_map 的遍历顺序不保证，排序让同一条命令的派发顺序稳定可复现。
			std::sort(subscribers.begin(), subscribers.end(),
				[](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

			for (const auto& subscriber : subscribers)
			{
				if (auto& handle = subscriber.second)
					handle(event, world);
			}

			++report.dispatched;
		}
		return report;
	}

	std::size_t UICommandBus::PendingCount() const
	{
		std::lock_guard<CriticalSectionLock> lock(m_mutex);
		return m_pending.size();
	}
}
