#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/TypeKey.h"
#include "glm\glm.hpp"


struct EntityDestroyedEvent
{
	Entity entity;

	EntityDestroyedEvent(Entity entity) :entity(entity) {}
};

struct PhysicsCollisionEvent
{
	Entity entityA;
	Entity entityB;
	glm::vec3 point;
	glm::vec3 normal;
};

struct QueryExistAnimatorGroupEvent
{
	Entity entity;
	bool isExist = false;
};

struct QueryExistPlayingAnimatorEvent
{
	Entity entity;
	bool isExist = false;
};

struct ResetAnimatorGroupEvent
{
	Entity entity;
};

struct PlayAnimatorEvent
{
	Entity entity;
	std::string animationName;
	int loopCount = 0;
};

HY_DECLARE_ECS_TYPE(EntityDestroyedEvent, "hy.engine.event.EntityDestroyed");
HY_DECLARE_ECS_TYPE(PhysicsCollisionEvent, "hy.engine.event.PhysicsCollision");
HY_DECLARE_ECS_TYPE(QueryExistAnimatorGroupEvent, "hy.engine.event.QueryExistAnimatorGroup");
HY_DECLARE_ECS_TYPE(QueryExistPlayingAnimatorEvent, "hy.engine.event.QueryExistPlayingAnimator");
HY_DECLARE_ECS_TYPE(ResetAnimatorGroupEvent, "hy.engine.event.ResetAnimatorGroup");
HY_DECLARE_ECS_TYPE(PlayAnimatorEvent, "hy.engine.event.PlayAnimator");
