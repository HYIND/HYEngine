#pragma once

#include "Input/EngineKey.h"

#include <bitset>
#include "CriticalSectionLock.h"

struct EngineInputFrame
{
    std::bitset<kEngineKeyCount> keysDown;
    std::bitset<kEngineKeyCount> keysPressed;
    std::bitset<kEngineKeyCount> keysReleased;

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
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

    bool IsKeyDown(EngineKey key) const;

    EngineInputFrame ReadFrame() const;
    EngineInputFrame ConsumeFrame();

private:
    mutable CriticalSectionLock m_mutex;
    EngineInputFrame m_frame;
    std::bitset<kEngineKeyCount> m_keysDown;
    std::bitset<kEngineKeyCount> m_pressedLatch;
    std::bitset<kEngineKeyCount> m_releasedLatch;
};
