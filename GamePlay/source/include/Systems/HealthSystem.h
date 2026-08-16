#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"

class HealthSystem :public System
{
public:
	virtual void onAttach(World& world) override;

private:
	void processDamageEvent(Entity source, Entity target, int damage);
};