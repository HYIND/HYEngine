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

class PhysicsScriptModule : public IScriptModule
{
public:
	explicit PhysicsScriptModule(World* world);
	virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;

public:
	bool ApplyForce(Entity entity, const glm::vec3& force);
	bool ApplyImpulse(Entity entity, const glm::vec3& impulse);
	bool ApplyTorque(Entity entity, const glm::vec3& torque);

	bool SetLinearVelocity(Entity entity, const glm::vec3& velocity);
	std::tuple<bool, glm::vec3> GetLinearVelocity(Entity entity) const;

	bool SetAngularVelocity(Entity entity, const glm::vec3& velocity);
	std::tuple<bool, glm::vec3> GetAngularVelocity(Entity entity) const;

private:
	btRigidBody* GetRigidBody(Entity entity) const;
	btRigidBody* GetDynamicBody(Entity entity) const;

private:
	World* _world = nullptr;
};
