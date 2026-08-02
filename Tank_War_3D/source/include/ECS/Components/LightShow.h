
#pragma once

#include "ECS/Core/IComponent.h"

struct LightShow :public IComponent
{
	float exposure = 0.f;		//曝光量
	float thresold = 500.f;		//激发阈值
	float lossRate = 100.f;		//流失速率

	bool isExcited = false;

	LightShow() {};
};