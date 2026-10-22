#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/System.h"
#include "Components/Physics.h"
#include "Components/Transform.h"
#include "glm\glm.hpp"

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"

#include "Script/IScriptModule.h"

// 射线检测结果结构
struct RaycastHit
{
	bool hit = false;
	Entity hitEntity;			// 命中的实体
	glm::vec3 hitPoint;         // 命中点
	glm::vec3 hitNormal;        // 法线
	float distance = 0.0f;      // 距离
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

class PhysicsSystem :public System
{

public:
	PhysicsSystem();

	virtual void onAttach(World& world) override;

	virtual void fixedUpdate(float dt) override;

	// 基础射线检测
	RaycastHit raycast(const glm::vec3& start, const glm::vec3& end);

	// 带碰撞组过滤的射线检测
	RaycastHit raycast(const glm::vec3& start, const glm::vec3& end,
		int collisionFilterGroup, int collisionFilterMask);

	// 检测所有命中物体（按距离排序）
	std::vector<RaycastHit> raycastAll(const glm::vec3& start, const glm::vec3& end);

	// 检测所有命中物体（带过滤）
	std::vector<RaycastHit> raycastAll(const glm::vec3& start, const glm::vec3& end,
		int collisionFilterGroup, int collisionFilterMask);

private:
	void checkNewEntity();
	void collisionCheck(float dt);
	void processCollisions();

	void syncWorldToPhysics();
	void syncPhysicsToWorld();

	void createBody(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime);
	void createRigidBody(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime);
	void createCharacter(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime);

	void updateRigidBody(Physics& physics, PhysicsRuntime& runtime);
	void updateCharacter(Physics& physics, PhysicsRuntime& runtime);

private:
	btDiscreteDynamicsWorld* phyWorld;
	float fixedTimeStep = 1.f / 60.f;
};
