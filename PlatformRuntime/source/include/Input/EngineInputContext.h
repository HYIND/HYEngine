#pragma once

#include "Input/EngineKey.h"

#include <bitset>
#include <string>
#include "CriticalSectionLock.h"

struct EngineInputFrame
{
    std::bitset<kEngineKeyCount> keysDown;
    std::bitset<kEngineKeyCount> keysPressed;
    std::bitset<kEngineKeyCount> keysReleased;

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;

    // 鼠标绝对位置（窗口坐标，像素）与渲染目标在窗口里的矩形（窗口坐标）。
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;

    // 本帧收到的文字（字符输入/IME 提交结果）。
    std::string textInput;

    // 本帧累计的滚轮增量（向上为正），UI 列表/文本滚动用。
    float mouseWheel = 0.0f;

    bool active = false;

    bool IsKeyDown(EngineKey key) const
    {
        return keysDown.test(EngineKeyIndex(key));
    }
    bool WasKeyPressed(EngineKey key) const
    {
        return keysPressed.test(EngineKeyIndex(key));
    }
    bool WasKeyReleased(EngineKey key) const
    {
        return keysReleased.test(EngineKeyIndex(key));
    }
};

class EngineInputContext
{
public:
    void SetKeyDown(EngineKey key, bool down);
    void ReleaseAllKeys();

    void NewFrame();    // 每帧由宿主调用一次（帧边界）：把持续状态快照成帧，并清掉已消费的边沿。

    void SetActive(bool active);
    void AddMouseDelta(float deltaX, float deltaY);

    // 滚轮：按帧累计，NewFrame 时清空。
    void PushMouseWheel(float delta);

    // 宿主每帧喂：窗口坐标下的鼠标位置、渲染目标在窗口里的矩形。
    void SetMousePosition(float x, float y);
    void SetViewportRect(float x, float y, float width, float height);
    // 宿主的字符输入（已经过 IME 组合的结果）；按帧清空。
    void PushTextInput(const std::string& utf8);

    bool IsKeyDown(EngineKey key) const;

    EngineInputFrame ReadFrame() const;
    EngineInputFrame ConsumeFrame();

private:
    mutable CriticalSectionLock m_mutex;
    EngineInputFrame m_frame;
    std::bitset<kEngineKeyCount> m_keysDown;
    std::bitset<kEngineKeyCount> m_pressedLatch;
    std::bitset<kEngineKeyCount> m_releasedLatch;

    float m_mouseWheel = 0.0f;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_viewportX = 0.0f;
    float m_viewportY = 0.0f;
    float m_viewportWidth = 0.0f;
    float m_viewportHeight = 0.0f;
    std::string m_textInput;
};
