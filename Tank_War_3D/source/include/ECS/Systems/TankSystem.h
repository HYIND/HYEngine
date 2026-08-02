#pragma once

#include "ECS/Core/System.h"

class TankSystem :public System
{
public:
	virtual void onAttach(World& world) override;
};