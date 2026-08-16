#pragma once

#include "ECSCore/IComponent.h"
#include "ECSCore/Entity.h"

struct LightFollow :public IComponent
{
	Entity target;
};