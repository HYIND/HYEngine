#pragma once

#include "ECSCore/Entity.h"
#include <sol/sol.hpp>

class IScriptModule
{
public:
	using ComponentRequestFunc = std::function<void(World&, Entity)>;

	virtual ~IScriptModule() = default;
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) = 0;
	virtual std::unordered_map<std::string, ComponentRequestFunc> ComponentRegister() { return {}; };

protected:
	static bool IsValid(World* world, Entity entity);
};