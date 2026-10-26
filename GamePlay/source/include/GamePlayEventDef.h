#pragma once

#include "ECSCore/Entity.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "glm\glm.hpp"
#include <optional>

struct DamageEvent
{
	Entity target;
	Entity source;       // 击杀者
	int damage;
};

struct HealthChangedEvent
{
	Entity entity;
	int currentHealth;
	int maxHealth;
};

struct EntityDeathEvent
{
	enum class DeathCause {
		Unknown,
		HealthDepleted,    // 血量耗尽
	};

	Entity entity;
	DeathCause cause = DeathCause::Unknown;
	std::optional<Entity> killer;
	glm::vec3 deathPosition;
};

struct PickUpHealEvent
{
	Entity picker;
};

struct WeaponShootEvent
{
	Entity source;
};

HY_DECLARE_ECS_TYPE(DamageEvent, "hy.gameplay.event.Damage");
HY_DECLARE_ECS_TYPE(HealthChangedEvent, "hy.gameplay.event.HealthChanged");
HY_DECLARE_ECS_TYPE(EntityDeathEvent, "hy.gameplay.event.EntityDeath");
HY_DECLARE_ECS_TYPE(PickUpHealEvent, "hy.gameplay.event.PickUpHeal");
HY_DECLARE_ECS_TYPE(WeaponShootEvent, "hy.gameplay.event.WeaponShoot");
