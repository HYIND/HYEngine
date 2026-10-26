#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"
#include "ECSCore/World.h"

#include <typeindex>
#include <unordered_map>
#include <set>
#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

#include "ECSCore/SystemTypes.h"

using EventTypeID = EcsTypeKey;

class EventSystem
{

private:
	template<typename T>
	struct EventType
	{
		static EventTypeID getID()
		{
			return GetEcsTypeKey<T>();
		}
	};

	struct IEventCallback {
		virtual ~IEventCallback() = default;
		virtual void invoke(void* eventData) = 0;
	};

	template<typename T>
	class EventCallback : public IEventCallback
	{
	public:
		using CallbackType = std::function<void(T&)>;

		explicit EventCallback(CallbackType callback)
			: _callback(std::move(callback)) {
		}

		void invoke(void* eventData) override
		{
			try
			{
				_callback(*static_cast<T*>(eventData));
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
	void Subscribe(std::function<void(Event&)> callback)
	{
		SystemTypeID systemId = SystemType<System>::getID();
		RegisterIdentity(m_systemNames, systemId, GetEcsTypeIdentity<System>());
		EventTypeIDToCallbackMap& map = SystemIDToEvent[systemId];

		EventTypeID eventid = EventType<Event>::getID();
		RegisterIdentity(m_eventNames, eventid, GetEcsTypeIdentity<Event>());
		map[eventid] = std::make_unique<EventCallback<Event>>(callback);
		EventTypeIDToSystemID[eventid].insert(systemId);
	}

	// 注册事件监听器
	template<typename Event>
	void Subscribe(SystemTypeID systemId, std::function<void(Event&)> callback)
	{
		EventTypeIDToCallbackMap& map = SystemIDToEvent[systemId];

		EventTypeID eventid = EventType<Event>::getID();
		RegisterIdentity(m_eventNames, eventid, GetEcsTypeIdentity<Event>());
		map[eventid] = std::make_unique<EventCallback<Event>>(callback);
		EventTypeIDToSystemID[eventid].insert(systemId);
	}

	// 广播事件
	template<typename Event>
	void Emit(Event& event)
	{
		EventTypeID id = EventType<Event>::getID();
		RegisterIdentity(m_eventNames, id, GetEcsTypeIdentity<Event>());
		if (EventTypeIDToSystemID.find(id) == EventTypeIDToSystemID.end())
			return;

		auto& entities = EventTypeIDToSystemID[id];
		for (auto entityId : entities)
		{
			EventTypeIDToCallbackMap& map = SystemIDToEvent[entityId];
			if (map.find(id) == map.end())
				continue;
			map[id]->invoke(&event);
		}
	}

	template<typename Event, typename... Args>
	void Emit(Args &&... args)
	{
		Event event(std::forward<Args>(args)...);
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
	template<typename Id>
	static void RegisterIdentity(std::unordered_map<Id, EcsTypeIdentity>& identities, Id id, const EcsTypeIdentity& identity)
	{
		auto [it, inserted] = identities.emplace(id, identity);
		if (!inserted && (it->second.stableName != identity.stableName || it->second.nativeName != identity.nativeName))
			throw std::runtime_error("ECS type identity collision: " + identity.stableName + " (" + identity.nativeName + ")");
	}

	using EventTypeIDToCallbackMap = std::unordered_map<EventTypeID, std::unique_ptr<IEventCallback>>;
	std::unordered_map<SystemTypeID, EventTypeIDToCallbackMap> SystemIDToEvent;		//通过实体找到关注的事件组
	std::unordered_map<EventTypeID, std::set<SystemTypeID>> EventTypeIDToSystemID;	//通过事件找到订阅的实体
	std::unordered_map<EventTypeID, EcsTypeIdentity> m_eventNames;
	std::unordered_map<SystemTypeID, EcsTypeIdentity> m_systemNames;
};
