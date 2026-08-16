#pragma once

#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"
#include <vector>

struct WeaponBasic : public IComponent
{
	std::string weaponName;
	int damage = 24;
	int maxAmmo = 30;
	int maxReserveAmmo = 120;
	int64_t fireRate = 200;			// 每发间隔（毫秒）
	int64_t reloadTime = 2000;		// 换弹时间（毫秒）
	float range = 1000.f;			// 有效范围
	enum Mode { Single, Auto, Burst } fireMode = Auto;// 射击模式：单发、连发、三连发
};