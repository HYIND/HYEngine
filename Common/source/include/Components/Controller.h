#pragma once

#include "ECSCore/IComponent.h"

// 控制器组件
struct Controller :public IComponent
{
	// 前后方向
	enum class ForwardBackward : int {
		NONE = 0,
		FORWARD,
		BACKWARD
	};

	// 左右方向
	enum class LeftRight : int {
		NONE = 0,
		LEFT,
		RIGHT
	};

	// 垂直方向
	enum class UpDown : int {
		NONE = 0,
		UP,
		DOWN
	};

	ForwardBackward forwardBackward = ForwardBackward::NONE;  // 前后
	LeftRight leftRight = LeftRight::NONE;                    // 左右
	UpDown upDown = UpDown::NONE;

	// 旋转（视角转向）
	float yaw = 90.0f;      // 水平旋转增量（鼠标）
	float pitch = 0.0f;     // 垂直旋转增量（鼠标）
	float mouseSensitivity = 0.8f;

	// 攻击意图
	bool wantToFire = false;	// 是否想射击
	bool wantToJump = false;	// 是否想跳跃
	bool wantToReload = false;	// 是否想跳跃

	int numberPress = -1;

	void setForwardBackward(ForwardBackward value) { forwardBackward = value; }
	void setLeftRight(LeftRight value) { leftRight = value; }
	void setUpDown(UpDown value) { upDown = value; }
	void setWantToFire(bool enabled) { wantToFire = enabled; }
	void setWantToJump(bool enabled) { wantToJump = enabled; }
	void setWantToReload(bool enabled) { wantToReload = enabled; }
	void setNumberPress(int number) { numberPress = number; }

	bool getWantToForWard() { return forwardBackward == ForwardBackward::FORWARD; }
	bool getWantToBackWard() { return forwardBackward == ForwardBackward::BACKWARD; }
	bool getWantToLeft() { return leftRight == LeftRight::LEFT; }
	bool getWantToRight() { return leftRight == LeftRight::RIGHT; }
	bool getWantToUp() { return upDown == UpDown::UP; }
	bool getWantToDown() { return upDown == UpDown::DOWN; }
	bool getWantToFire() { return wantToFire; }
	bool getWantToJump() { return wantToJump; }
	bool getWantToReload() { return wantToReload; }
	int getNumberPress() { return numberPress; }

	void addMouseDelta(float mouseDeltaX, float mouseDeltaY)
	{
		float newYaw = yaw + mouseDeltaX * mouseSensitivity;
		float newPitch = pitch - mouseDeltaY * mouseSensitivity;

		if (newPitch > 89.0f)
			newPitch = 89.0f;
		if (newPitch < -89.0f)
			newPitch = -89.0f;

		yaw = newYaw;
		pitch = newPitch;
	}
};
