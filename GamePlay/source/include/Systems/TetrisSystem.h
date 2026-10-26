#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Entity.h"
#include "Script/IScriptModule.h"

class TetrisModule : public IScriptModule
{
public:
	explicit TetrisModule(World* world) :_world(world){}
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;

private:
	World* _world = nullptr;
};

class TetrisSystem :public System
{
public:
	virtual void onAttach(World& world);
	virtual void update(float deltaTime) override;
};

HY_DECLARE_ECS_TYPE(TetrisSystem, "hy.gameplay.system.Tetris");
