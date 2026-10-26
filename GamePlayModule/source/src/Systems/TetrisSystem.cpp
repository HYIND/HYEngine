#include "Systems/TetrisSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "CommonSystems.h"
#include "Systems/ScriptSystem.h"


void TetrisModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	sol::table Tetris = lua["Tetris"].get_or_create<sol::table>();
}

void TetrisSystem::onAttach(World& world)
{
	if (auto scriptSystem = world.getSystem<ScriptSystem>())
		scriptSystem->RegisterScriptModule("TetrisModule", std::make_unique<TetrisModule>(&world));
}

void TetrisSystem::update(float deltaTime)
{}

