#pragma once

#include "Input/EngineKey.h"

// GLFW 的键码 -> 引擎按键。编辑器是 GLFW 宿主，事件在 GLFW 回调里拿到，
// 所以这张表放在宿主侧：引擎不认识 GLFW，Game 那边用的是 Win32 的 VK 表。
//
// glfwKey 用的是 GLFW_KEY_* / GLFW_MOUSE_BUTTON_* 的原始值，这里不 include GLFW，
// 免得把第三方头文件带进宿主以外的地方。
EngineKey EngineKeyFromGlfwKey(int glfwKey);
EngineKey EngineKeyFromGlfwMouseButton(int glfwButton);
