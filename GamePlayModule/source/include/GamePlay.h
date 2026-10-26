#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _DEBUG
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "AssetRuntime_d.lib")
#pragma comment(lib, "PlatformRuntime_d.lib")
#pragma comment(lib, "VulkanRenderEngine_d.lib")
#pragma comment(lib, "net_d.lib")
#pragma comment(lib, "public_d.lib")
#pragma comment(lib, "assimp_d.lib")

#else
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "AssetRuntime.lib")
#pragma comment(lib, "PlatformRuntime.lib")
#pragma comment(lib, "VulkanRenderEngine.lib")
#pragma comment(lib, "net.lib")
#pragma comment(lib, "public.lib")
#pragma comment(lib, "assimp.lib")

#endif
