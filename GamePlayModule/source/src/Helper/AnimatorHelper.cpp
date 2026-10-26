#include "Helper/AnimatorHelper.h"

using namespace AnimatorHelper;

bool AnimatorHelper::HasAnimatorGroup(const Entity& entity)
{
	if (!entity)
		return false;

	QueryExistAnimatorGroupEvent event;
	event.entity = entity;
	event.isExist = false;
	entity.getWorld()->Emit(event);
	return event.isExist;
}

bool AnimatorHelper::HasPlayingAnimator(const Entity& entity)
{
	if (!entity)
		return false;

	QueryExistPlayingAnimatorEvent event;
	event.entity = entity;
	event.isExist = false;
	entity.getWorld()->Emit(event);
	return event.isExist;
}

void AnimatorHelper::ResetAnimatorGroup(const Entity& entity)
{
	if (!entity)
		return;

	ResetAnimatorGroupEvent event;
	event.entity = entity;
	entity.getWorld()->Emit(event);
}

void AnimatorHelper::PlayAnimator(const Entity& entity, const std::string& animatorName, int loopCount)
{
	if (!entity)
		return;

	PlayAnimatorEvent event;
	event.entity = entity;
	event.animationName = animatorName;
	event.loopCount = loopCount;
	entity.getWorld()->Emit(event);
}
