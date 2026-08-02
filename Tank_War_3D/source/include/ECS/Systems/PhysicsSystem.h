#pragma once

#include "ECS/Core/Entity.h"
#include "ECS/Core/System.h"
#include "ECS/Components/Physics.h"
#include "ECS/Components/Transform.h"
#include "glm\glm.hpp"

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"

// 射线检测结果结构
struct RaycastHit
{
	bool hit = false;
	Entity hitEntity;			// 命中的实体
	glm::vec3 hitPoint;         // 命中点
	glm::vec3 hitNormal;        // 法线
	float distance = 0.0f;      // 距离
};

class PhysicsSystem :public System
{

public:
	PhysicsSystem();
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

	void syncWorldToPhysics(float dt_second);
	void syncPhysicsToWorld();

	void createBody(Entity entity, Physics& physics, Transform& trans);
	void createRigidBody(Entity entity, Physics& physics, Transform& trans);
	void createCharacter(Entity entity, Physics& physics, Transform& trans);

private:
	btDiscreteDynamicsWorld* phyWorld;
	float fixedTimeStep = 1.f / 60.f;
};
