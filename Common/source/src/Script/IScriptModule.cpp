#include "Script/IScriptModule.h"
#include "ECSCore/World.h"

bool IScriptModule::IsValid(World* world, Entity entity)
{
	return world && world->IsValidWorldEntity(entity);
}