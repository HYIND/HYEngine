#pragma once

#include "ECSCore/Types.h"

template<typename T>
struct SystemType
{
	static SystemTypeID getID()
	{
		return GetEcsTypeKey<T>();
	}
};
