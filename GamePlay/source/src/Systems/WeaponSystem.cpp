#include "Systems/WeaponSystem.h"
#include "Systems/PhysicsSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "Helper/AnimatorHelper.h"
#include "Helper/Tools.h"
#include "GamePlayEventDef.h"

void WeaponSystem::onAttach(World& world)
{

}

void WeaponSystem::update(float deltaTime)
{
	auto entities = m_world->getEntitiesWith<TagPlayer, WeaponOwner>();

	for (auto& entity : entities)
	{
		auto& weaponOwner = entity.getComponent<WeaponOwner>();
		if (weaponOwner.curSwitchCoolDown > 0.f)
			weaponOwner.curSwitchCoolDown = std::max(0.f, weaponOwner.curSwitchCoolDown - deltaTime);

		if (weaponOwner.curSwitchCoolDown > 0.f)
			continue;

		processSwitchWeapon(entity, weaponOwner);

		if (weaponOwner.curSwitchCoolDown > 0.f)
			continue;

		auto weapon = weaponOwner.getCurrentWeapon();
		if (!weapon)
			continue;

		auto* weaponBasic = weapon.tryGetComponent<WeaponBasic>();
		auto* weaponState = weapon.tryGetComponent<WeaponState>();
		if (!weaponBasic || !weaponState)
			continue;

		processWeaponInput(entity, weapon, weaponOwner, *weaponBasic, *weaponState);
		processWeaponReload(entity, weapon, *weaponBasic, *weaponState);

		if (AnimatorHelper::HasAnimatorGroup(weapon))
		{
			if (!AnimatorHelper::HasPlayingAnimator(weapon))
				AnimatorHelper::PlayAnimator(weapon, "Idle", 0);
		}
	}
}

void WeaponSystem::processSwitchWeapon(Entity& player, WeaponOwner& owner)
{
	auto* controller = player.tryGetComponent<Controller>();
	if (!controller)
		return;

	int number = controller->getNumberPress();
	if (number >= 0)
	{
		int originIndex = owner.currentIndex;
		owner.switchTo(number);

		if (originIndex != owner.currentIndex)
		{
			std::cout << std::format("[WeaponSystem] switchTo {}\n", owner.getCurrentWeapon().getComponent<WeaponBasic>().weaponName);
			if (auto weapon = owner.getCurrentWeapon())
			{
				if (AnimatorHelper::HasAnimatorGroup(weapon))
				{
					AnimatorHelper::ResetAnimatorGroup(weapon);
					AnimatorHelper::PlayAnimator(weapon, "Draw");
				}
			}
		}
	}
}

void WeaponSystem::processWeaponInput(Entity& player, Entity& weapon, WeaponOwner& owner, WeaponBasic& basic, WeaponState& state)
{
	auto* controller = player.tryGetComponent<Controller>();
	if (!controller)
		return;

	if (controller->getWantToReload() && !state.isReloading)
	{
		if (state.currentAmmo < basic.maxAmmo && state.reserveAmmo > 0)
		{
			std::cout << std::format("[WeaponSystem] reloading\n");
			state.isReloading = true;
			state.reloadTimer = Tool::GetTimestampMilliseconds();
			if (AnimatorHelper::HasAnimatorGroup(weapon))
			{
				AnimatorHelper::ResetAnimatorGroup(weapon);
				AnimatorHelper::PlayAnimator(weapon, "Reload");
			}
			return;
		}
	}

	static auto CanFire = [](WeaponBasic& basic, WeaponState& state)->bool
		{
			auto now = Tool::GetTimestampMilliseconds();
			bool ammo = state.currentAmmo > 0;
			bool reload = !state.isReloading;
			bool cooldown = (now - state.lastFireTime) > basic.fireRate;
			return ammo && reload && cooldown;
		};

	if (controller->getWantToFire() && CanFire(basic, state))
	{
		fireBullet(player, basic, state);
		if (AnimatorHelper::HasAnimatorGroup(weapon))
		{
			AnimatorHelper::ResetAnimatorGroup(weapon);
			AnimatorHelper::PlayAnimator(weapon, "Shot");
		}
	}
}

void WeaponSystem::processWeaponReload(Entity& player, Entity& weapon, WeaponBasic& basic, WeaponState& state)
{
	if (state.isReloading)
	{
		if (Tool::GetTimestampMilliseconds() - state.reloadTimer > basic.reloadTime)
		{
			int addAmmo = std::min(std::min(basic.maxAmmo, state.reserveAmmo), basic.maxAmmo - state.currentAmmo);
			state.currentAmmo += addAmmo;
			state.reserveAmmo -= addAmmo;
			state.isReloading = false;
			state.reloadTimer = 0;
			std::cout << std::format("[WeaponSystem] reload done  ammo={}/{}\n", state.currentAmmo, state.reserveAmmo);
		}
	}
	else
	{
		if (state.currentAmmo <= 0 && state.reserveAmmo > 0)
		{
			state.isReloading = true;
			state.reloadTimer = Tool::GetTimestampMilliseconds();
			std::cout << std::format("[WeaponSystem] auto reload start ammo={}/{}\n", state.currentAmmo, state.reserveAmmo);
			if (AnimatorHelper::HasAnimatorGroup(weapon))
			{
				AnimatorHelper::ResetAnimatorGroup(weapon);
				AnimatorHelper::PlayAnimator(weapon, "Reload");
			}
		}
	}
}

void WeaponSystem::fireBullet(Entity& shooter, WeaponBasic& basic, WeaponState& state)
{
	state.currentAmmo -= 1;
	state.lastFireTime = Tool::GetTimestampMilliseconds();
	std::cout << std::format("[WeaponSystem] Fire ammo={}/{}\n", state.currentAmmo, state.reserveAmmo);

	auto fireRayCast = [&](const glm::vec3& origin, const glm::vec3& end)-> void
		{
			auto* physicSystem = m_world->getSystem<PhysicsSystem>();
			if (physicSystem)
			{
				m_world->Emit<WeaponShootEvent>(WeaponShootEvent{ .source = shooter });
				RaycastHit res = physicSystem->raycast(origin, end);
				if (res.hit && res.hitEntity)
				{
					std::cout << std::format("[WeaponSystem] Fire hit entityid={} distance={}\n", res.hitEntity.getId(), res.distance);

					if (res.hitEntity.hasComponent<TagCharacter>())
					{
						auto* health = res.hitEntity.tryGetComponent<Health>();
						if (health && !(health->isInvulnerable))
						{
							m_world->Emit<DamageEvent>(DamageEvent{
								.target = res.hitEntity,
								.source = shooter,
								.damage = basic.damage
								});
							return;
						}
					}
				}
			}
		};

	if (shooter.hasComponent<Transform>())
	{
		auto& trans = shooter.getComponent<Transform>();

		glm::vec3 origin = trans.position;
		glm::vec3 forward = trans.getDirection();
		glm::vec3 end = origin + std::max(0.f, basic.range) * forward;

		fireRayCast(origin, end);
	}
}

