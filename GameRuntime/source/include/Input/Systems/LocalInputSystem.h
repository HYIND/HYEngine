#pragma once

#include "ECSCore/System.h"

class LocalInputSystem :public System
{
public:
	virtual void preUpdate(float deltaTime) override;
};