#pragma once

#include "ECS/Core/Types.h"
#include <atomic>

inline std::atomic<SystemTypeID> g_SystemTypeCounter = 0;

template<typename T>
struct SystemType
{
	static SystemTypeID getID()
	{
		static const SystemTypeID id = g_SystemTypeCounter.fetch_add(1);
		return id;
	}
};
