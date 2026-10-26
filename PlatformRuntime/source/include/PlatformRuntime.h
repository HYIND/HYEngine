#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "EngineRuntime.h"


#ifdef _DEBUG
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "VulkanRenderEngine_d.lib")
// 运行时负责加载项目模块，而模块加载实现和场景 codec 放在一起，
// 因此两个库在链接期互相引用。
#pragma comment(lib, "AssetRuntime_d.lib")

#else
#pragma comment(lib, "Common.lib")
#pragma comment(lib, "VulkanRenderEngine.lib")
#pragma comment(lib, "AssetRuntime.lib")

#endif
