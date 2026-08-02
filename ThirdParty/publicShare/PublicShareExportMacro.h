#pragma once

#ifdef _WIN32
#ifdef PUBLICSHARE_EXPORTS
#define PUBLICSHARE_API __declspec(dllexport)
#else
#define PUBLICSHARE_API __declspec(dllimport)
#endif
#else
#define PUBLICSHARE_API
#endif