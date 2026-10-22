#pragma once

#include "IScriptModule.h"
#include "CommonComponent.h"
#include <tuple>

class GLMMathModule : public IScriptModule
{
public:
	explicit GLMMathModule() = default;
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;

private:
	void RegisterVector(sol::state_view lua);
	void RegisterQuaternion(sol::state_view lua);
	void RegisterMatrix4(sol::state_view lua);
};

class TransformScriptModule : public IScriptModule
{
public:
	explicit TransformScriptModule(World* world);
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;
	std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> ComponentRegister() override;

public:
	std::tuple<bool, glm::vec3> GetPosition(Entity entity) const;
	std::tuple<bool, glm::vec3> GetDirection(Entity entity) const;

	bool SetPosition(Entity entity, float x, float y, float z);
	bool Translate(Entity entity, float x, float y, float z);

private:
	World* _world = nullptr;
};
