#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/System.h"
#include "Components/Physics.h"
#include "Components/Transform.h"
#include "glm\glm.hpp"

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"

#include "Script/IScriptModule.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

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

// ───────────── 碰撞体调试显示 ─────────────
// 画的是 Bullet 世界里实际参与碰撞的形状（btCollisionWorld::debugDrawObject），缩放、复合形状、角色控制器的规则都和碰撞一致。
// 编辑状态下 StepEditorFrame 每帧跑一次 fixedUpdate(0)，刚体照常创建 / 同步，所以编辑时也能看到。

struct PhysicsDebugDrawOptions
{
	bool filterByEntities = false;			// true：只画 onlyEntities 里的实体（升序）
	std::vector<EntityID> onlyEntities;

	glm::vec3 cameraPosition{ 0.0f };
	float maxDistance = 0.0f;				// > 0：包围盒离相机超过这个距离的物体不画
	bool useFrustum = false;				// true：包围盒整个在视锥外的物体不画（只看裁剪空间的 x / y / w，GL / Vulkan 约定都行）
	glm::mat4 viewProj{ 1.0f };
	float frustumMargin = 1.15f;			// 视锥放宽一点：结果晚一两帧才画出来，相机转动时边缘不会缺

	float triangleMeshRadius = 60.0f;		// 三角网格（凹形状）只画相机周围这个半径内的三角形；<= 0 画全部
	size_t maxLines = 60000;				// 线段总数上限，超过就停（PhysicsDebugLines::truncated）

	bool drawAabb = false;					// 额外画 Bullet 的世界 AABB
	bool drawContacts = false;				// 额外画接触点（只有播放时有，编辑时不模拟）
};

enum class PhysicsDebugLineKind : uint8_t { Shape, Aabb, Contact };

struct PhysicsDebugObject
{
	Entity entity;
	Physics::BodyType bodyType = Physics::BodyType::Static;
	bool isSensor = false;
	bool isCharacter = false;
	bool sleeping = false;
};

struct PhysicsDebugLine
{
	glm::vec3 a{ 0.0f };
	glm::vec3 b{ 0.0f };
	uint32_t object = UINT32_MAX;			// PhysicsDebugLines::objects 的下标；接触点为 UINT32_MAX
	uint16_t child = UINT16_MAX;			// 第几个碰撞形状（Physics::collisionShape.children 的下标）；AABB / 接触点为 UINT16_MAX
	PhysicsDebugLineKind kind = PhysicsDebugLineKind::Shape;
};

struct PhysicsDebugLines
{
	std::vector<PhysicsDebugObject> objects;
	std::vector<PhysicsDebugLine> lines;	// 世界坐标
	bool truncated = false;					// 到了 maxLines，后面的没画
};

// 凸包的边（btConvexHullShape 没有多面体特征时，Bullet 的 getEdge 只会把点按顺序连成折线，所以自己算一次缓存起来）
struct PhysicsDebugHullEdges
{
	int numPoints = 0;
	size_t hash = 0;
	std::vector<btVector3> vertices;		// 未缩放
	std::vector<std::pair<int, int>> edges;
	uint64_t lastUsed = 0;
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

	// 碰撞体调试显示：把要画的线段收集到 out（会先清空）。必须在场景线程调用
	void CollectDebugLines(const PhysicsDebugDrawOptions& options, PhysicsDebugLines& out);

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

	// CollectDebugLines 用：凸包边的缓存（按形状指针，点数 / 内容变了就重算；很久没用到的清掉）
	std::unordered_map<const btCollisionShape*, PhysicsDebugHullEdges> m_debugHullCache;
	uint64_t m_debugDrawCount = 0;
};

HY_DECLARE_ECS_TYPE(PhysicsSystem, "hy.engine.system.Physics");
