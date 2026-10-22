#pragma once

#include "ECSCore/IComponent.h"

struct Renderable : public IComponent
{
	bool visible = true;
};