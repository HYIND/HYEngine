
#pragma once

#include <stdint.h>
#include <functional>
#include "ECS/Core/IComponent.h"

struct LifeTime :public IComponent
{
	float remainingTime = 0.f;
	bool autoDestroy = true;
	std::function<void()> onComplete;

	LifeTime() {};
	LifeTime(float remainSecond)
	{
		remainingTime = std::max(0.f, remainSecond) * 1000;
	}

	void update(float dt)
	{
		remainingTime -= dt;
		if (remainingTime <= 0.0f && onComplete) {
			onComplete();
		}
	}
};