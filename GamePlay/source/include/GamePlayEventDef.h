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
