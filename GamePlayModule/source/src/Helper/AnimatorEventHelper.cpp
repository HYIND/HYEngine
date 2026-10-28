#include "Helper/AnimatorEventHelper.h"

using namespace AnimatorEventHelper;

bool AnimatorEventHelper::HasAnimatorGroup(const Entity& entity)
{
	if (!entity)
		return false;

	QueryExistAnimatorGroupEvent event;
	event.entity = entity;
	event.isExist = false;
	entity.getWorld()->Emit(event);
	return event.isExist;
}

bool AnimatorEventHelper::HasPlayingAnimator(const Entity& entity)
{
	if (!entity)
		return false;

	QueryExistPlayingAnimatorEvent event;
	event.entity = entity;
	event.isExist = false;
	entity.getWorld()->Emit(event);
	return event.isExist;
}

void AnimatorEventHelper::ResetAnimatorGroup(const Entity& entity)
{
	if (!entity)
		return;

	ResetAnimatorGroupEvent event;
	event.entity = entity;
	entity.getWorld()->Emit(event);
}

void AnimatorEventHelper::PlayAnimator(const Entity& entity, const std::string& animatorName, int loopCount)
{
	if (!entity)
		return;

	PlayAnimatorEvent event;
	event.entity = entity;
	event.animationName = animatorName;
	event.loopCount = loopCount;
	entity.getWorld()->Emit(event);
}
