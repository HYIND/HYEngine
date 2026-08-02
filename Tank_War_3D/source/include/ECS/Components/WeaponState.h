#pragma once

#include "ECS/Core/IComponent.h"
#include "ECS/Core/Entity.h"
#include "RenderModel.h"
#include <vector>

struct WeaponState : public IComponent
{
	int currentAmmo = 30;
	int reserveAmmo = 120;
	int64_t lastFireTime = 0;      // 上次射击时间（用于冷却判断）
	int64_t reloadTimer = 0;
	bool isReloading = false;

	WeaponState() {}
	WeaponState(int currentAmmo, int reserveAmmo) :currentAmmo(currentAmmo), reserveAmmo(reserveAmmo) {}
};