#pragma once

#include "Types.h"

// 获取组件类型的唯一ID（编译时计算）
template<typename T>
struct ComponentType
{
	static ComponentTypeID getId()
	{
		return GetEcsTypeKey<T>();
	}

	static const char* getName()
	{
		static const std::string name = GetEcsTypeName<T>();
		return name.c_str();
	}
};
