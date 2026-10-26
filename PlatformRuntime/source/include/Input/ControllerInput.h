#pragma once

#include <cstdint>

#include "Components/Controller.h"
#include "Input/Components/PlayerInput.h"
#include "Input/EngineInputContext.h"

// 输入帧 -> PlayerInput。帧里只有原始按键与这一帧的鼠标增量，这里不做任何语义翻译。
// 每个控制实体都拿同一份帧：键是绝对状态，增量是"这一帧鼠标动了多少"，
// 都属于设备层面的事，跟实体排第几个无关。
inline void SyncPlayerInput(PlayerInput& input, const EngineInputFrame& frame)
{
    if (!frame.active)
    {
        input.reset();
        return;
    }

    input.keysDown = frame.keysDown;
    input.keysPressed = frame.keysPressed;
    input.keysReleased = frame.keysReleased;
    input.active = true;
    input.setMouseInput(static_cast<int>(frame.mouseDeltaX), static_cast<int>(frame.mouseDeltaY));
}

// 引擎自带 Controller 的默认映射：WASD 走位、Z/X 升降、空格跳、R 换弹、鼠标左键开火、数字键 0-9。
// 这是"默认实现"的约定，不是引擎规则 —— 自定义 Controller 不要调这个函数，
inline void ApplyDefaultControllerInput(const PlayerInput& input, Controller& controller)
{
    const bool forward = input.IsDown(EngineKey::W);
    const bool backward = input.IsDown(EngineKey::S);
    const bool left = input.IsDown(EngineKey::A);
    const bool right = input.IsDown(EngineKey::D);
    const bool up = input.IsDown(EngineKey::Z);
    const bool down = input.IsDown(EngineKey::X);

    if (forward == backward)
        controller.setForwardBackward(Controller::ForwardBackward::NONE);
    else
        controller.setForwardBackward(forward ? Controller::ForwardBackward::FORWARD
                                              : Controller::ForwardBackward::BACKWARD);

    if (left == right)
        controller.setLeftRight(Controller::LeftRight::NONE);
    else
        controller.setLeftRight(left ? Controller::LeftRight::LEFT : Controller::LeftRight::RIGHT);

    if (up == down)
        controller.setUpDown(Controller::UpDown::NONE);
    else
        controller.setUpDown(up ? Controller::UpDown::UP : Controller::UpDown::DOWN);

    controller.setWantToJump(input.IsDown(EngineKey::Space));
    controller.setWantToFire(input.IsDown(EngineKey::MouseLeft));
    controller.setWantToReload(input.IsDown(EngineKey::R));

    int numberPress = -1;
    for (uint32_t index = 0; index < 10; ++index)
    {
        const auto key = static_cast<EngineKey>(static_cast<uint32_t>(EngineKey::Num0) + index);
        if (input.IsDown(key))
        {
            numberPress = static_cast<int>(index);
            break;
        }
    }
    controller.setNumberPress(numberPress);

    controller.addMouseDelta(static_cast<float>(input.mouseDeltaX), static_cast<float>(input.mouseDeltaY));
}
