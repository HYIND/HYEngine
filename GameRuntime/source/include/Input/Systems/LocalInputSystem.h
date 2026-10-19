#pragma once

#include "ECSCore/System.h"
#include "CommonSystems.h"

class InputScriptModule : public IScriptModule
{
public:
	explicit InputScriptModule(World* world);
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;
	virtual std::unordered_map<std::string, ComponentRequestFunc> ComponentRegister() override;

private:
	void RegisterInput(sol::state_view lua, sol::usertype<Entity>& entityType);
	void RegisterController(sol::state_view lua, sol::usertype<Entity>& entityType);

private:
	World* _world = nullptr;
};

class LocalInputSystem :public System
{
public:
	virtual void onAttach(World& world) override;
	virtual void preUpdate(float deltaTime) override;
};