#pragma once

#include "ECSCore/World.h"
#include "CommonEventDef.h"

namespace AnimatorHelper
{
	bool HasAnimatorGroup(const Entity& entity);
	bool HasPlayingAnimator(const Entity& entity);
	bool QueryPlayingAnimator(const Entity& entity, std::string& animatorName);
	void ResetAnimatorGroup(const Entity& entity);
	void PlayAnimator(const Entity& entity, const std::string& animatorName, int loopCount = 1);
}