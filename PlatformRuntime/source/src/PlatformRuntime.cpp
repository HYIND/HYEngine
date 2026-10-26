
#include "PlatformRuntime.h"

#include "imconfig.h"
#include "imgui.h"

// imconfig.h 把 ImGui 的 context 指针 GImGui 定义成了线程局部的 ImGuiTLS，
thread_local ImGuiContext* ImGuiTLS = nullptr;
