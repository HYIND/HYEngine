#pragma once

#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

struct LightFollow :public IComponent
{
	Entity target;
};

HY_DECLARE_ECS_TYPE(LightFollow, "gameplay.light_follow");
