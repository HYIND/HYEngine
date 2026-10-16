#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _DEBUG
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "VulkanRenderEngine_d.lib")
#pragma comment(lib, "GamePlay_d.lib")
#pragma comment(lib, "GameRuntime_d.lib")
#else
#pragma comment(lib, "Common.lib")
#pragma comment(lib, "VulkanRenderEngine.lib")
#pragma comment(lib, "GamePlay.lib")
#pragma comment(lib, "GameRuntime.lib")
#endif