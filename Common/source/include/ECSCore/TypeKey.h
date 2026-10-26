#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <typeinfo>

using EcsTypeKey = uint64_t;

struct EcsTypeIdentity
{
	std::string stableName;
	std::string nativeName;
};

template<typename T>
struct EcsTypeName
{
	static std::string get() { return typeid(T).name(); }
};

inline EcsTypeKey MakeEcsTypeKey(std::string_view name)
{
	// FNV-1a 64-bit. The full name is retained by registries to detect collisions.
	EcsTypeKey hash = 14695981039346656037ull;
	for (unsigned char value : name)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash;
}

template<typename T>
inline std::string GetEcsTypeName()
{
	return EcsTypeName<T>::get();
}

template<typename T>
inline std::string GetEcsNativeTypeName()
{
	return typeid(T).name();
}

template<typename T>
inline EcsTypeIdentity GetEcsTypeIdentity()
{
	return { GetEcsTypeName<T>(), GetEcsNativeTypeName<T>() };
}

template<typename T>
inline EcsTypeKey GetEcsTypeKey()
{
	static const EcsTypeKey key = MakeEcsTypeKey(GetEcsTypeName<T>());
	return key;
}

// Declare at global scope after the type definition. Names are persisted API identifiers,
// so keep them stable when renaming C++ types.
#define HY_DECLARE_ECS_TYPE(Type, StableName) \
template<> struct EcsTypeName<Type> { static std::string get() { return StableName; } }
