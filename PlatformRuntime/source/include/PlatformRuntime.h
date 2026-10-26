#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "EngineRuntime.h"


#ifdef _DEBUG
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "VulkanRenderEngine_d.lib")
#pragma comment(lib, "AssetRuntime_d.lib")

#else
#pragma comment(lib, "Common.lib")
#pragma comment(lib, "VulkanRenderEngine.lib")
#pragma comment(lib, "AssetRuntime.lib")

#endif
