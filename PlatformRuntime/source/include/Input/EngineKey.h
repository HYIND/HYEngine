#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// 引擎认识的按键。这里只给"哪个键"一个平台无关的身份，不规定哪个键做什么用：
// 语义（前进/开火/技能1）由 Controller 自己决定查哪些键。
//
// 宿主的职责是把平台的原生键码翻译成这里的一项：
//   Win32 宿主：EngineKeyFromVirtualKey(wParam)
//   GLFW 宿主（编辑器）：Editor/source/src/EditorGlfwInput.cpp 里的 GLFW_KEY_* 表
enum class EngineKey : uint32_t
{
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Escape, Tab, CapsLock, Space, Enter, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,

    ArrowUp, ArrowDown, ArrowLeft, ArrowRight,

    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,

    // 鼠标键也当按键处理：同样有"按住"和"刚按下"两种状态，
    // 这样 Controller 查开火/瞄准时不用为鼠标单开一套接口。
    MouseLeft, MouseRight, MouseMiddle, MouseX1, MouseX2,

    Minus, Equal, LeftBracket, RightBracket, Backslash,
    Semicolon, Quote, Comma, Period, Slash, Grave,

    Count
};

inline constexpr size_t kEngineKeyCount = static_cast<size_t>(EngineKey::Count);

inline size_t EngineKeyIndex(EngineKey key)
{
    return static_cast<size_t>(key);
}

inline bool IsEngineKeyValid(EngineKey key)
{
    return key != EngineKey::Unknown && EngineKeyIndex(key) < kEngineKeyCount;
}

inline bool IsMouseEngineKey(EngineKey key)
{
    return key >= EngineKey::MouseLeft && key <= EngineKey::MouseX2;
}

// Win32 虚拟键码 -> EngineKey（鼠标键也走这里：VK_LBUTTON 等）
EngineKey EngineKeyFromVirtualKey(int virtualKey);

// 名字 -> EngineKey，给脚本用："W"、"Space"、"MouseLeft"、"Num3"。认不出来返回 Unknown。
EngineKey EngineKeyFromName(std::string_view name);
