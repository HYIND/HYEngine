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

// UI 专用的瞬态输入：文字与滚轮。
// 两样和按键/鼠标位置不一样——它们是"事件"，只能被消费一次
struct EngineUiInput
{
    std::string text;
    float wheel = 0.0f;
};

class EngineInputContext
{
public:
    void SetActive(bool active);

    void SetKeyDown(EngineKey key, bool down);
    void ReleaseAllKeys();

    void NewFrame();

    void SetMousePosition(float x, float y);
    void SetMouseDelta(float deltaX, float deltaY);
    void SetViewportRect(float x, float y, float width, float height);

    bool IsKeyDown(EngineKey key) const;

    void PushMouseWheel(float delta);
    void PushTextInput(const std::string& utf8);

    EngineInputFrame ReadFrame() const;
    EngineInputFrame ConsumeFrame();

    EngineUiInput TakeUiInput();

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
