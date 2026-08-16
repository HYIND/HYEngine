#pragma once

#include <cstdint>
#include <typeinfo>
#include <bitset>
#include "Types.h"

inline ComponentTypeID g_componentCounter = 0;

// 获取组件类型的唯一ID（编译时计算）
template<typename T>
struct ComponentType
{
	static ComponentTypeID getId()
	{
		static const ComponentTypeID id = g_componentCounter++;
		return id;
	}

	static const char* getName()
	{
		return typeid(T).name();
	}
};