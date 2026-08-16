#pragma once

#include "RenderBase.h"
#include "RenderEngine/D2DTools.h"
#include "Helper/Tools.h"
#include "ECSCore/IComponent.h"

struct GIFAnimator : public RenderBase2D
{

	int width = 0;
	int height = 0;

	float opacity = 1.0f;

	int64_t startTime = 0;
	float giftotalTime = 1.f;
	int loopCount = 1;

	GIFINFO* gifInfo = nullptr;

	GIFAnimator() {}
	GIFAnimator(int width, int height, GIFINFO* gifInfo)
		: GIFAnimator(width, height, gifInfo, 1, 1.0f) {
	}
	GIFAnimator(int width, int height, GIFINFO* gifInfo, int loopCount, float opacity = 1.0f) :
		gifInfo(gifInfo), loopCount(loopCount), opacity(opacity)
	{
		this->width = std::max(0, width);
		this->height = std::max(0, height);
		if (gifInfo)
			giftotalTime = gifInfo->getDefaultMsTime();

		startTime = Tool::GetTimestampMilliseconds();
	}
};
