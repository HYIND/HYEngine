#pragma once

#include "ECSCore/System.h"
#include "GamePlayComponents.h"

class WeaponSystem :public System
{
public:
	virtual void onAttach(World& world) override;
	virtual void update(float deltaTime) override;

private:
	void processSwitchWeapon(Entity& player, WeaponOwner& owner);
	void processWeaponInput(Entity& player, Entity& weapon, WeaponOwner& owner, WeaponBasic& basic, WeaponState& state);
	void processWeaponReload(Entity& player, Entity& weapon, WeaponBasic& basic, WeaponState& state);

private:
	void fireBullet(Entity& shooter, WeaponBasic& basic, WeaponState& state);
};