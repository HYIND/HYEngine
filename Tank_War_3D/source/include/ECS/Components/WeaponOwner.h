#pragma once

#include "ECS/Core/IComponent.h"
#include "ECS/Core/Entity.h"
#include "RenderModel.h"
#include <vector>

struct WeaponOwner : public IComponent {
	std::vector<Entity> weapons;

	float switchCoolDown = 800.f;

	float curSwitchCoolDown = 0.f;
	int currentIndex = -1;               // -1 表示空手

	Entity weaponSocket;

	WeaponOwner(){}

	Entity getCurrentWeapon()
	{
		if (weapons.empty() || currentIndex >= weapons.size())
			return Entity();
		return weapons[currentIndex];
	}

	void switchTo(int index)
	{
		if (index >= 0 && index < weapons.size())
		{
			if (index == currentIndex)
				return;

			currentIndex = index;

			// 重置武器状态（换弹中断、准星恢复等）
			if (auto* state = getCurrentWeapon().tryGetComponent<WeaponState>()) {
				state->isReloading = false;
				state->reloadTimer = 0.0f;
			}
			curSwitchCoolDown = switchCoolDown;
		}
	}
};