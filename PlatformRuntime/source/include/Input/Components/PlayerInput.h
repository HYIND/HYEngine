#pragma once

#include <bitset>

#include "ECSCore/IComponent.h"
#include "Input/EngineKey.h"

// 玩家输入：只记录"哪些键按着、哪些键刚按下、哪些键刚抬起"，不含任何玩法语义。
//
// 关心"W 是前进还是后退"的是 Controller：它自己决定查哪几个键。
// 所以同一个 PlayerInput 可以被不同 Controller 按不同映射读取，
// 引擎这边不需要知道任何绑定表。
struct PlayerInput : public IComponent
{
    std::bitset<kEngineKeyCount> keysDown;      // 当前按住
    std::bitset<kEngineKeyCount> keysPressed;   // 本帧刚按下（边沿）
    std::bitset<kEngineKeyCount> keysReleased;  // 本帧刚抬起（边沿）

    int mouseDeltaX = 0;
    int mouseDeltaY = 0;

    bool active = false;                        // 宿主窗口是否有输入焦点

    PlayerInput() = default;

    bool IsDown(EngineKey key) const
    {
        return IsEngineKeyValid(key) && keysDown.test(EngineKeyIndex(key));
    }

    bool WasPressed(EngineKey key) const
    {
        return IsEngineKeyValid(key) && keysPressed.test(EngineKeyIndex(key));
    }

    bool WasReleased(EngineKey key) const
    {
        return IsEngineKeyValid(key) && keysReleased.test(EngineKeyIndex(key));
    }

    void setMouseInput(int deltaX, int deltaY)
    {
        mouseDeltaX = deltaX;
        mouseDeltaY = deltaY;
    }

    void reset()
    {
        keysDown.reset();
        keysPressed.reset();
        keysReleased.reset();
        mouseDeltaX = 0;
        mouseDeltaY = 0;
        active = false;
    }
};

HY_DECLARE_ECS_TYPE(PlayerInput, "engine.player_input");
