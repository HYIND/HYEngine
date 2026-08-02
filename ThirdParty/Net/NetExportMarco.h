#pragma once

#ifdef _WIN32
#ifdef NET_EXPORTS
#define NET_API __declspec(dllexport)
#else
#define NET_API __declspec(dllimport)
#endif
#else
#define NET_API
#endif