#pragma once

#include "ECS/Core/System.h"
#include "ECS/Core/Entity.h"
#include "ECS/Core/World.h"

#include <typeindex>
#include <unordered_map>
#include <set>
#include <vector>
#include <functional>

#include "ECS/Core/SystemTypes.h"

using EventTypeID = uint32_t;

inline std::atomic<EventTypeID> g_EventTypeCounter = 0;

class EventSystem
{

private:
	template<typename T>
	struct EventType
	{
		static EventTypeID getID()
		{
			static const EventTypeID id = g_EventTypeCounter.fetch_add(1);
			return id;
		}
	};

	struct IEventCallback {
		virtual ~IEventCallback() = default;
		virtual void invoke(const void* eventData) = 0;
	};

	template<typename T>
	class EventCallback : public IEventCallback
	{
	public:
		using CallbackType = std::function<void(const T&)>;

		explicit EventCallback(CallbackType callback)
			: _callback(std::move(callback)) {
		}

		void invoke(const void* eventData) override
		{
			try
			{
				_callback(*static_cast<const T*>(eventData));
			}
			catch (const std::exception&)
			{
			}
		}
	private:
		CallbackType _callback;
	};


public:

	// 注册事件监听器
	template<typename System, typename Event>
	void Subscribe(std::function<void(const Event&)> callback)
	{
		SystemTypeID systemId = SystemType<System>::getID();
		EventTypeIDToCallbackMap& map = SystemIDToEvent[systemId];

		EventTypeID eventid = EventType<Event>::getID();
		map[eventid] = std::make_unique<EventCallback<Event>>(callback);
		EventTypeIDToSystemID[eventid].insert(systemId);
	}

	// 注册事件监听器
	template<typename Event>
	void Subscribe(SystemTypeID systemId, std::function<void(const Event&)> callback)
	{
		EventTypeIDToCallbackMap& map = SystemIDToEvent[systemId];

		EventTypeID eventid = EventType<Event>::getID();
		map[eventid] = std::make_unique<EventCallback<Event>>(callback);
		EventTypeIDToSystemID[eventid].insert(systemId);
	}

	// 广播事件
	template<typename Event>
	void Emit(const Event& event)
	{
		EventTypeID id = EventType<Event>::getID();
		if (EventTypeIDToSystemID.find(id) == EventTypeIDToSystemID.end())
			return;

		auto& entities = EventTypeIDToSystemID[id];
		for (auto entityId : entities)
		{
			EventTypeIDToCallbackMap& map = SystemIDToEvent[entityId];
			if (map.find(id) == map.end())
				return;
			map[id]->invoke(&event);
		}
	}

	template<typename Event, typename... Args>
	void Emit(Args &&... args)
	{
		const Event event(std::forward<Args>(args)...);
		Emit<Event>(event);
	}

	void RemoveSystem(SystemTypeID systemId)
	{
		auto EntityIDToEvent_it = SystemIDToEvent.find(systemId);
		if (EntityIDToEvent_it == SystemIDToEvent.end())
			return;

		EventTypeIDToCallbackMap& map = EntityIDToEvent_it->second;
		for (auto it = map.begin(); it != map.end(); it++)
		{
			EventTypeID id = it->first;
			if (EventTypeIDToSystemID.find(id) != EventTypeIDToSystemID.end())
				EventTypeIDToSystemID[id].erase(systemId);
		}
		SystemIDToEvent.erase(EntityIDToEvent_it);
	}

private:
	using EventTypeIDToCallbackMap = std::unordered_map<EventTypeID, std::unique_ptr<IEventCallback>>;
	std::unordered_map<SystemTypeID, EventTypeIDToCallbackMap> SystemIDToEvent;		//通过实体找到关注的事件组
	std::unordered_map<EventTypeID, std::set<SystemTypeID>> EventTypeIDToSystemID;	//通过事件找到订阅的实体
};