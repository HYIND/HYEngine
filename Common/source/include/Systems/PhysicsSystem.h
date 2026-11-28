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
#include <memory>
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

	float triangleMeshRadius = 60.0f;		// 三角网格（凹形状）只画相机周围这个半径内的三角形；<= 0 画全部。线段数不设上限

	bool drawAabb = false;					// 额外画 Bullet 的世界 AABB
	bool drawContacts = false;				// 额外画接触点（只有播放时有，编辑时不模拟）
};

enum class PhysicsDebugLineKind : uint8_t { Shape, Aabb, Contact };

// 一个物体的碰撞形状线框，在物体的局部空间里（刚体的世界变换只有旋转和平移，缩放已经在形状里，setLocalScaling）。
// 按顶点 + 边存：位置相同的顶点合成一个，同一个碰撞形状里重复的边只留一条。
// 绘制时每个顶点只变换一次，边直接取两个端点的结果（三角网格一个顶点平均被约 6 条边共用）。
// 按物体缓存：形状重建了（PhysicsRuntime::debugRevision 变了）、缩放变了、三角网格的查询中心离相机太远了才重新生成。
// 生成后不再修改，界面线程拿着 shared_ptr 直接读，不用加锁
struct PhysicsDebugShapeLines
{
	struct Edge
	{
		uint32_t a = 0;						// vertices 的下标
		uint32_t b = 0;
		uint16_t child = UINT16_MAX;		// 第几个碰撞形状（Physics::collisionShape.children 的下标）
	};
	std::vector<glm::vec3> vertices;
	std::vector<Edge> edges;
};

struct PhysicsDebugObject
{
	Entity entity;
	Physics::BodyType bodyType = Physics::BodyType::Static;
	bool isSensor = false;
	bool isCharacter = false;
	bool sleeping = false;
	glm::mat4 model{ 1.0f };								// 刚体 / 角色的世界变换（不含缩放）
	std::shared_ptr<const PhysicsDebugShapeLines> lines;	// 局部空间的形状线段（缓存，多帧共用）
};

// 每帧现算的世界坐标线段：AABB、接触点（数量少）
struct PhysicsDebugLine
{
	glm::vec3 a{ 0.0f };
	glm::vec3 b{ 0.0f };
	uint32_t object = UINT32_MAX;			// PhysicsDebugLines::objects 的下标；接触点为 UINT32_MAX
	uint16_t child = UINT16_MAX;
	PhysicsDebugLineKind kind = PhysicsDebugLineKind::Aabb;
};

struct PhysicsDebugLines
{
	std::vector<PhysicsDebugObject> objects;
	std::vector<PhysicsDebugLine> lines;	// 世界坐标的 AABB / 接触点
	size_t shapeLineCount = 0;				// 所有物体的形状边数之和
	size_t shapeVertexCount = 0;			// 所有物体的形状顶点数之和
	size_t rebuiltObjects = 0;				// 这一次重新生成了几个物体的线段（其余用缓存）
	float collectMs = 0.0f;					// CollectDebugLines 用时
};

// 一个物体的线段缓存
struct PhysicsDebugShapeCacheEntry
{
	uint64_t revision = 0;					// 生成时的 PhysicsRuntime::debugRevision
	btVector3 scaling{ 1, 1, 1 };			// 生成时根形状的 getLocalScaling()
	bool cameraDependent = false;			// 有三角网格：只取了相机周围的三角形
	float queryRadius = 0.0f;
	btVector3 queryCenter{ 0, 0, 0 };		// 生成时相机在物体局部空间里的位置
	std::shared_ptr<const PhysicsDebugShapeLines> lines;
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

	// CollectDebugLines 用：每个物体局部空间线段的缓存（按实体 id；很久没画到的清掉）
	std::unordered_map<EntityID, PhysicsDebugShapeCacheEntry> m_debugShapeCache;
	uint64_t m_debugDrawCount = 0;
	uint64_t m_bodyRevision = 0;			// createBody 每次加 1，写进 PhysicsRuntime::debugRevision
};

HY_DECLARE_ECS_TYPE(PhysicsSystem, "hy.engine.system.Physics");
