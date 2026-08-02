#pragma once

#include "ECS/Components/AllComponent.h"
#include "ECS/Core/Entity.h"
#include "glm\glm.hpp"
#include <optional>


struct EntityDestroyedEvent
{
	Entity entity;

	EntityDestroyedEvent(Entity entity) :entity(entity) {}
};

struct BulletDestroyedEvent
{
	enum class DestroyReason {
		HitTarget,     // 击中目标
		Timeout,       // 超时
		MaxBound       // 达到反弹上限
	};

	Entity bullet;
	Entity shooter;
	DestroyReason reason;  // 销毁原因
	glm::vec3 position;		   // 销毁位置
};

struct TankDestroyedEvent
{
	Entity tank;
	std::optional<Entity> killer;  // 击杀者
	glm::vec3 position;
};

struct DamageEvent
{
	Entity target;
	Entity source;       // 击杀者
	int damage;
};

struct PhysicsCollisionEvent
{
	Entity entityA;
	Entity entityB;
	glm::vec3 point;
	glm::vec3 normal;
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

struct WallDestroyedEvent
{
	Entity wall;
	std::optional<Entity> killer;  // 击杀者
	glm::vec3 position;
};

struct PickUpHealEvent
{
	Entity picker;
};

struct WeaponShootEvent
{
	Entity source;
};
