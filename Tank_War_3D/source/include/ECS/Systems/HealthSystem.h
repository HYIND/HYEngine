#pragma once

#include "ECS/Core/System.h"
#include "ECS/Core/Entity.h"

class HealthSystem :public System
{
public:
	virtual void onAttach(World& world) override;

private:
	void processDamageEvent(Entity source, Entity target, int damage);
};