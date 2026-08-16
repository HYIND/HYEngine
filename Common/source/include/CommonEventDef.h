#pragma once

#include "ECSCore/Entity.h"
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