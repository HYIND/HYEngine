#include "Systems/PhysicsSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "Helper/Tools.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>
#include <chrono>
#include <functional>
#include "Systems/ScriptSystem.h"
#include "bullet/LinearMath/btConvexHullComputer.h"

const float gravityScale = 1.f;

static glm::vec3 BulletToGlm(const btVector3& v) {
	return glm::vec3(v.x(), v.y(), v.z());
}

static glm::quat BulletToGlm(const btQuaternion& q) {
	return glm::quat(q.w(), q.x(), q.y(), q.z());
}

static btVector3 GlmToBullet(const glm::vec3& v) {
	return btVector3(v.x, v.y, v.z);
}

static btQuaternion GlmToBullet(const glm::quat& q) {
	return btQuaternion(q.x, q.y, q.z, q.w);
}

class RaycastCallback : public btCollisionWorld::RayResultCallback
{
public:
	RaycastHit& hitResult;

	glm::vec3 rayStart;
	glm::vec3 rayEnd;
	float totalRayDistance;

	RaycastCallback(RaycastHit& result, const glm::vec3& start, const glm::vec3& end)
		: hitResult(result)
	{
		hitResult.hit = false;
		rayStart = start;
		rayEnd = end;
		totalRayDistance = glm::length(rayStart - rayEnd);
		m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::CharacterFilter;
		m_collisionFilterMask = btBroadphaseProxy::AllFilter;
	}

	virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,
		bool normalInWorldSpace) override
	{
		// 只处理第一次命中（最近距离）
		if (rayResult.m_hitFraction < m_closestHitFraction)
		{
			m_closestHitFraction = rayResult.m_hitFraction;

			Entity* hitentity = static_cast<Entity*>(rayResult.m_collisionObject->getUserPointer());
			if (hitentity && hitentity->isValid())
				hitResult.hitEntity = Entity(*hitentity);// 获取命中的实体

			// 记录命中信息
			hitResult.hit = true;
			hitResult.distance = rayResult.m_hitFraction * totalRayDistance;
			hitResult.hitPoint = rayStart + (rayEnd - rayStart) * rayResult.m_hitFraction;// 计算命中点和法线

			if (normalInWorldSpace)
			{
				hitResult.hitNormal = BulletToGlm(rayResult.m_hitNormalLocal);
			}
			else
			{
				// 转换到世界空间
				btVector3 worldNormal = rayResult.m_collisionObject->getWorldTransform().getBasis() * rayResult.m_hitNormalLocal;
				hitResult.hitNormal = BulletToGlm(worldNormal);
			}

			// 可选：获取命中的部件
			//hitResult.hitPart = rayResult.m_partId;

			return m_closestHitFraction;
		}
		return m_closestHitFraction;
	}
};

// 2. 创建回调（收集所有命中）
class AllRaycastCallback : public btCollisionWorld::RayResultCallback
{
public:
	std::vector<RaycastHit>& hits;

	glm::vec3 rayStart;
	glm::vec3 rayEnd;
	float totalRayDistance;

	AllRaycastCallback(std::vector<RaycastHit>& hitList, const glm::vec3& start, const glm::vec3& end)
		: hits(hitList)
	{
		rayStart = start;
		rayEnd = end;
		totalRayDistance = glm::length(rayStart - rayEnd);
		m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::CharacterFilter;
		m_collisionFilterMask = btBroadphaseProxy::AllFilter;
	}

	virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,
		bool normalInWorldSpace) override
	{
		RaycastHit hitResult;

		Entity* hitentity = static_cast<Entity*>(rayResult.m_collisionObject->getUserPointer());
		if (hitentity && hitentity->isValid())
			hitResult.hitEntity = Entity(*hitentity);// 获取命中的实体

		// 记录命中信息
		hitResult.hit = true;
		hitResult.distance = rayResult.m_hitFraction * totalRayDistance;
		hitResult.hitPoint = rayStart + (rayEnd - rayStart) * rayResult.m_hitFraction;// 计算命中点和法线

		if (normalInWorldSpace)
		{
			hitResult.hitNormal = BulletToGlm(rayResult.m_hitNormalLocal);
		}
		else
		{
			// 转换到世界空间
			btVector3 worldNormal = rayResult.m_collisionObject->getWorldTransform().getBasis() * rayResult.m_hitNormalLocal;
			hitResult.hitNormal = BulletToGlm(worldNormal);
		}

		// 可选：获取命中的部件
		//hitResult.hitPart = rayResult.m_partId;

		hits.push_back(hitResult);


		return 1.0f;// 继续检测，收集所有命中
	}
};

PhysicsScriptModule::PhysicsScriptModule(World* world) :_world(world) {}

btRigidBody* PhysicsScriptModule::GetRigidBody(Entity entity) const
{
	if (!IsValid(_world, entity))
		return nullptr;

	auto* runtime = entity.tryGetComponent<PhysicsRuntime>();
	if (!runtime)
		return nullptr;

	return runtime->body;
}

// 力/速度只对 Dynamic 刚体有意义，Static/Kinematic 的速度会被 PhysicsSystem 同步覆盖
btRigidBody* PhysicsScriptModule::GetDynamicBody(Entity entity) const
{
	btRigidBody* body = GetRigidBody(entity);
	if (!body || body->isStaticOrKinematicObject())
		return nullptr;

	body->activate(true);
	return body;
}

bool PhysicsScriptModule::ApplyForce(Entity entity, const glm::vec3& force)
{
	btRigidBody* body = GetDynamicBody(entity);
	if (!body)
		return false;

	body->applyCentralForce(GlmToBullet(force));
	return true;
}

bool PhysicsScriptModule::ApplyImpulse(Entity entity, const glm::vec3& impulse)
{
	btRigidBody* body = GetDynamicBody(entity);
	if (!body)
		return false;

	body->applyCentralImpulse(GlmToBullet(impulse));
	return true;
}

bool PhysicsScriptModule::ApplyTorque(Entity entity, const glm::vec3& torque)
{
	btRigidBody* body = GetDynamicBody(entity);
	if (!body)
		return false;

	body->applyTorque(GlmToBullet(torque));
	return true;
}

bool PhysicsScriptModule::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
{
	btRigidBody* body = GetDynamicBody(entity);
	if (!body)
		return false;

	body->setLinearVelocity(GlmToBullet(velocity));
	return true;
}

std::tuple<bool, glm::vec3> PhysicsScriptModule::GetLinearVelocity(Entity entity) const
{
	btRigidBody* body = GetRigidBody(entity);
	if (!body)
		return { false, glm::vec3(0.0f) };

	return { true, BulletToGlm(body->getLinearVelocity()) };
}

bool PhysicsScriptModule::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
{
	btRigidBody* body = GetDynamicBody(entity);
	if (!body)
		return false;

	body->setAngularVelocity(GlmToBullet(velocity));
	return true;
}

std::tuple<bool, glm::vec3> PhysicsScriptModule::GetAngularVelocity(Entity entity) const
{
	btRigidBody* body = GetRigidBody(entity);
	if (!body)
		return { false, glm::vec3(0.0f) };

	return { true, BulletToGlm(body->getAngularVelocity()) };
}

void PhysicsScriptModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	lua.new_usertype<RaycastHit>(
		"RaycastHit", sol::no_constructor,
		"hit", sol::readonly_property([](const RaycastHit& h) { return h.hit; }),
		"hit_entity", sol::readonly_property([](const RaycastHit& h) { return h.hitEntity; }),
		"hit_point", sol::readonly_property([](const RaycastHit& h) { return h.hitPoint; }),
		"hit_normal", sol::readonly_property([](const RaycastHit& h) { return h.hitNormal; }),
		"distance", sol::readonly_property([](const RaycastHit& h) { return h.distance; })
	);

	auto raycast = [this](const glm::vec3& start, const glm::vec3& end) {
		if (_world)
		{
			if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
				return physicsSystem->raycast(start, end);
		}
		return RaycastHit();
		};
	auto raycastAll = [this](const glm::vec3& start, const glm::vec3& end, sol::this_state state) {
		sol::state_view view(state);
		sol::table result = view.create_table();
		if (_world)
		{
			if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
			{
				auto hits = physicsSystem->raycastAll(start, end);
				for (size_t i = 0; i < hits.size(); ++i)
					result[i + 1] = hits[i];
			}
		}
		return result;
		};

	auto applyForce = [this](Entity entity, const glm::vec3& force) {
		return ApplyForce(entity, force);
		};
	auto applyImpulse = [this](Entity entity, const glm::vec3& impulse) {
		return ApplyImpulse(entity, impulse);
		};
	auto applyTorque = [this](Entity entity, const glm::vec3& torque) {
		return ApplyTorque(entity, torque);
		};
	auto setLinearVelocity = [this](Entity entity, const glm::vec3& velocity) {
		return SetLinearVelocity(entity, velocity);
		};
	auto getLinearVelocity = [this](Entity entity) {
		return GetLinearVelocity(entity);
		};
	auto setAngularVelocity = [this](Entity entity, const glm::vec3& velocity) {
		return SetAngularVelocity(entity, velocity);
		};
	auto getAngularVelocity = [this](Entity entity) {
		return GetAngularVelocity(entity);
		};

	sol::table physics = lua.create_table();
	physics.set_function("raycast", raycast);
	physics.set_function("raycast_all", raycastAll);
	physics.set_function("apply_force", applyForce);
	physics.set_function("apply_impulse", applyImpulse);
	physics.set_function("apply_torque", applyTorque);
	physics.set_function("set_linear_velocity", setLinearVelocity);
	physics.set_function("get_linear_velocity", getLinearVelocity);
	physics.set_function("set_angular_velocity", setAngularVelocity);
	physics.set_function("get_angular_velocity", getAngularVelocity);
	lua["Physics"] = physics;

	entityType.set("apply_force", applyForce);
	entityType.set("apply_impulse", applyImpulse);
	entityType.set("apply_torque", applyTorque);
	entityType.set("set_linear_velocity", setLinearVelocity);
	entityType.set("get_linear_velocity", getLinearVelocity);
	entityType.set("set_angular_velocity", setAngularVelocity);
	entityType.set("get_angular_velocity", getAngularVelocity);
}


btCollisionShape* createConvexHullShape(const std::vector<glm::vec3>& vertices)
{
	if (vertices.empty()) {
		// 没有顶点，回退到胶囊体
		std::cerr << "Warning: ConvexHull has no vertices, falling back to Capsule" << std::endl;
		return new btCapsuleShape(0.4f, 1.7f);
	}

	// 1. 创建凸包形状
	btConvexHullShape* hullShape = new btConvexHullShape();

	// 2. 逐个添加顶点
	for (const auto& v : vertices) {
		btVector3 btV(v.x, v.y, v.z);
		hullShape->addPoint(btV);
	}

	// 3. 优化凸包
	// 移除内部点，只保留凸包表面的顶点
	hullShape->optimizeConvexHull();

	// hullShape->setMargin(0.01f);  // 设置碰撞边距

	return hullShape;
}

btCollisionShape* createTriangleShape(const std::vector<glm::vec3>& vertices, const std::vector<unsigned int>& indices)
{
	if (vertices.empty()) {
		// 没有顶点，回退到胶囊体
		std::cerr << "Warning: ConvexHull has no vertices, falling back to Capsule" << std::endl;
		return new btCapsuleShape(0.4f, 1.7f);
	}

	// 创建三角形网格（仅限静态物体！）
	btTriangleMesh* terrainMesh = new btTriangleMesh();

	// 遍历地形网格的三角形
	for (int i = 0; i < indices.size(); i += 3)
	{
		btVector3 v0(vertices[indices[i]].x, vertices[indices[i]].y, vertices[indices[i]].z);
		btVector3 v1(vertices[indices[i + 1]].x, vertices[indices[i + 1]].y, vertices[indices[i + 1]].z);
		btVector3 v2(vertices[indices[i + 2]].x, vertices[indices[i + 2]].y, vertices[indices[i + 2]].z);
		terrainMesh->addTriangle(v0, v1, v2);
	}

	// 创建碰撞形状
	btBvhTriangleMeshShape* terrainShape = new btBvhTriangleMeshShape(terrainMesh, true);

	return terrainShape;
}

btCollisionShape* createShape(CollisionShape::ChildShape& childShape)
{
	btCollisionShape* childShapePtr = nullptr;
	switch (childShape.shapeType) {
	case ShapeType::Box:
		childShapePtr = new btBoxShape(GlmToBullet(childShape.size));
		break;
	case ShapeType::Sphere:
		childShapePtr = new btSphereShape(childShape.size.x);
		break;
	case ShapeType::Capsule:
		childShapePtr = new btCapsuleShape(childShape.size.x, childShape.size.y);
		break;
	case ShapeType::Cylinder:
		childShapePtr = new btCylinderShape(GlmToBullet(childShape.size));
		break;
	case ShapeType::ConvexHull:
		childShapePtr = createConvexHullShape(childShape.vertices);  // 复用凸包生成
		break;
	case ShapeType::TriangleMesh:
		childShapePtr = createTriangleShape(childShape.vertices, childShape.indices);
		break;
	default:
		childShapePtr = new btBoxShape(btVector3(0.5, 0.5, 0.5));
		break;
	}
	return childShapePtr;
}

btCollisionShape* createCompoundShape(std::vector<std::shared_ptr<CollisionShape::ChildShape>>& childShapes, PhysicsRuntime& runtime)
{
	if (childShapes.empty())
	{
		std::cerr << "Warning: Compound has no children, falling back to Box" << std::endl;
		return new btBoxShape(btVector3(0.5, 0.5, 0.5));
	}

	// 创建组合体
	btCompoundShape* compoundShape = new btCompoundShape();

	// 遍历子形状
	for (auto& child : childShapes)
	{
		// 创建子形状
		btCollisionShape* childShape = createShape(*child);

		// 计算局部变换
		btTransform localTransform;
		localTransform.setIdentity();
		localTransform.setOrigin(GlmToBullet(child->position));
		localTransform.setRotation(GlmToBullet(child->rotation));

		// 添加到组合体
		runtime.childShapePtrs.push_back(childShape);
		compoundShape->addChildShape(localTransform, childShape);
	}

	return compoundShape;
}

PhysicsSystem::PhysicsSystem()
{
	// 物理世界初始化
	btBroadphaseInterface* broadphase = new btDbvtBroadphase();
	btDefaultCollisionConfiguration* config = new btDefaultCollisionConfiguration();
	btCollisionDispatcher* dispatcher = new btCollisionDispatcher(config);
	btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();

	phyWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, config);
	phyWorld->setGravity(btVector3(0, -10, 0) * gravityScale);

	btDbvtBroadphase* bp = static_cast<btDbvtBroadphase*>(broadphase);
	if (bp) {
		bp->getOverlappingPairCache()->setInternalGhostPairCallback(
			new btGhostPairCallback()
		);
	}
}

void PhysicsSystem::onAttach(World& world)
{
	if (auto scriptSystem = world.getSystem<ScriptSystem>())
	{
		scriptSystem->RegisterScriptModule("PhysicsSystem", std::make_unique<PhysicsScriptModule>(&world));
	}
}

void PhysicsSystem::fixedUpdate(float dt)
{
	checkNewEntity();
	collisionCheck(dt);
}

RaycastHit PhysicsSystem::raycast(const glm::vec3& start, const glm::vec3& end)
{
	// 使用默认碰撞组（检测所有物体）
	return raycast(start, end,
		btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::CharacterFilter,
		btBroadphaseProxy::AllFilter);
}

RaycastHit PhysicsSystem::raycast(const glm::vec3& start, const glm::vec3& end,
	int collisionFilterGroup, int collisionFilterMask)
{
	RaycastHit result;

	// 1. 准备射线起点和终点
	btVector3 btStart = GlmToBullet(start);
	btVector3 btEnd = GlmToBullet(end);

	// 3. 执行射线检测
	RaycastCallback callback(result, start, end);
	callback.m_collisionFilterGroup = collisionFilterGroup;
	callback.m_collisionFilterMask = collisionFilterMask;

	phyWorld->rayTest(btStart, btEnd, callback);

	return result;
}

std::vector<RaycastHit> PhysicsSystem::raycastAll(const glm::vec3& start, const glm::vec3& end)
{
	return raycastAll(start, end,
		btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::CharacterFilter,
		btBroadphaseProxy::AllFilter);
}

std::vector<RaycastHit> PhysicsSystem::raycastAll(const glm::vec3& start, const glm::vec3& end,
	int collisionFilterGroup, int collisionFilterMask)
{
	std::vector<RaycastHit> results;

	// 1. 准备射线
	btVector3 btStart = GlmToBullet(start);
	btVector3 btEnd = GlmToBullet(end);

	// 3. 执行射线检测
	AllRaycastCallback callback(results, start, end);
	callback.m_collisionFilterGroup = collisionFilterGroup;
	callback.m_collisionFilterMask = collisionFilterMask;

	phyWorld->rayTest(btStart, btEnd, callback);

	// 按距离排序
	std::sort(results.begin(), results.end(),
		[](const RaycastHit& a, const RaycastHit& b) {
			return a.distance < b.distance;
		});

	return results;
}

void PhysicsSystem::checkNewEntity()
{
	auto entities = m_world->getEntitiesWith<Physics, Transform>();
	for (auto entity : entities)
	{
		if (!entity.hasComponent<PhysicsRuntime>())
		{
			auto& physics = entity.getComponent<Physics>();
			if (physics.collisionShape.children.empty())
				continue;

			auto& trans = entity.getComponent<Transform>();
			auto& runtime = entity.addComponent<PhysicsRuntime>();
			createBody(entity, physics, trans, runtime);
		}
	}
}

void PhysicsSystem::syncWorldToPhysics()
{
	auto entities = m_world->getEntitiesWith<Physics, Transform, PhysicsRuntime>();

	for (auto entity : entities)
	{
		auto& physics = entity.getComponent<Physics>();
		auto& trans = entity.getComponent<Transform>();
		auto& runtime = entity.getComponent<PhysicsRuntime>();

		if (physics.isCharacter && runtime.character)
		{
			if (runtime.ghostObject)
			{
				btTransform currentTrans = runtime.ghostObject->getWorldTransform();
				currentTrans.setRotation(GlmToBullet(trans.rotation));
				runtime.ghostObject->setWorldTransform(currentTrans);
				if (!runtime.ghostObject->isActive())
					runtime.ghostObject->activate(true);
			}

			// 从 Movement 组件读取输入
			if (auto* move = entity.tryGetComponent<Movement>())
			{
				float stepDisplacement = physics.walkSpeed * fixedTimeStep;
				btVector3 walkDir = GlmToBullet(trans.rotation * move->currentMoveDirection * stepDisplacement);
				runtime.character->setWalkDirection(walkDir);

				// 跳跃
				if (move->canJump && move->currentWantJump && runtime.character->canJump())
					runtime.character->jump();
			}
		}

		if (physics.forceSyncTransform || physics.bodyType == Physics::BodyType::Kinematic)
		{
			btTransform newTrans;
			newTrans.setIdentity();
			newTrans.setOrigin(GlmToBullet(trans.position));
			newTrans.setRotation(GlmToBullet(trans.rotation));

			if (runtime.body)
			{
				runtime.body->setWorldTransform(newTrans);
				runtime.body->getMotionState()->setWorldTransform(newTrans);
				runtime.body->setInterpolationWorldTransform(newTrans);
				runtime.body->setLinearVelocity(btVector3(0, 0, 0));
				runtime.body->setAngularVelocity(btVector3(0, 0, 0));
				runtime.body->activate(true);
				phyWorld->updateSingleAabb(runtime.body);
			}
			if (runtime.ghostObject)
			{
				runtime.ghostObject->setWorldTransform(newTrans);
				runtime.ghostObject->setInterpolationWorldTransform(newTrans);
				runtime.ghostObject->activate(true);
				phyWorld->updateSingleAabb(runtime.ghostObject);
			}
			physics.forceSyncTransform = false;
		}

		auto& shape = runtime.shapePtr;
		if (shape)
		{
			if (shape->getLocalScaling() != GlmToBullet(trans.scale))
			{
				btVector3 newScale = GlmToBullet(trans.scale);
				shape->setLocalScaling(newScale);

				// Dynamic 物体需要重新计算惯性
				if (physics.bodyType == Physics::BodyType::Dynamic)
				{
					if (runtime.body)
					{
						float mass = physics.mass;
						btVector3 localInertia(0, 0, 0);
						shape->calculateLocalInertia(mass, localInertia);
						runtime.body->setMassProps(mass, localInertia);
						runtime.body->updateInertiaTensor();
					}
				}
				if (shape->getShapeType() == COMPOUND_SHAPE_PROXYTYPE) {
					btCompoundShape* compound = static_cast<btCompoundShape*>(shape);
					compound->recalculateLocalAabb();
				}
			}
		}

		if (physics.forceRecalculate)
		{
			auto clearShapeState = [&](PhysicsRuntime& runtime)
				{
					if (runtime.motionState)
					{
						delete runtime.motionState;
						runtime.motionState = nullptr;
					}
					for (auto ptr : runtime.childShapePtrs)
						DeleteCollisionShape(ptr);
					runtime.childShapePtrs.clear();
					if (runtime.shapePtr)
					{
						DeleteCollisionShape(runtime.shapePtr);
						runtime.shapePtr = nullptr;
					}
				};
			auto clearCharacter = [&](PhysicsRuntime& runtime)
				{
					if (runtime.character || runtime.ghostObject)
					{
						if (runtime.character)
						{
							phyWorld->removeAction(runtime.character);
							delete runtime.character;
							runtime.character = nullptr;
						}
						if (runtime.ghostObject)
						{
							phyWorld->removeCollisionObject(runtime.ghostObject);
							delete runtime.ghostObject;
							runtime.ghostObject = nullptr;
						}
						clearShapeState(runtime);
					}
				};
			auto clearRigidBody = [&](PhysicsRuntime& runtime)
				{
					if (runtime.body)
					{
						phyWorld->removeRigidBody(runtime.body);
						delete runtime.body;
						runtime.body = nullptr;
						clearShapeState(runtime);
					}
				};

			if (!physics.isCharacter)
			{
				if (runtime.character || runtime.ghostObject)
					clearCharacter(runtime);

				if (runtime.body)
					updateRigidBody(physics, runtime);
				else
					createBody(entity, physics, trans, runtime);
			}
			else
			{
				if (runtime.body)
					clearRigidBody(runtime);

				if (runtime.character && runtime.ghostObject)
					updateCharacter(physics, runtime);
				else
					createBody(entity, physics, trans, runtime);
			}

			physics.forceRecalculate = false;
		}

	}
}

void PhysicsSystem::collisionCheck(float dt)
{
	float dt_second = dt / 1000.f;

	syncWorldToPhysics();

	if (dt_second > 0.f)
		phyWorld->stepSimulation(dt_second, 8, fixedTimeStep);	// 执行物理模拟

	processCollisions();

	syncPhysicsToWorld();
}

void PhysicsSystem::processCollisions()
{
	int numManifolds = phyWorld->getDispatcher()->getNumManifolds();
	for (int i = 0; i < numManifolds; i++)
	{
		btPersistentManifold* contactManifold = phyWorld->getDispatcher()->getManifoldByIndexInternal(i);
		const btCollisionObject* obA = contactManifold->getBody0();
		const btCollisionObject* obB = contactManifold->getBody1();

		int numContacts = contactManifold->getNumContacts();
		if (numContacts == 0) continue;

		// 获取 Entity 指针（在 createBody 中设置）
		Entity* entityA = static_cast<Entity*>(obA->getUserPointer());
		Entity* entityB = static_cast<Entity*>(obB->getUserPointer());

		if (!entityA || !entityB) continue;

		// 获取第一个接触点
		btManifoldPoint& pt = contactManifold->getContactPoint(0);
		btVector3 point = pt.getPositionWorldOnA();
		btVector3 normal = pt.m_normalWorldOnB;

		PhysicsCollisionEvent collisionEvent{
			.entityA = *entityA,
			.entityB = *entityB,
			.point = BulletToGlm(point),
			.normal = BulletToGlm(normal)
		};
		m_world->Emit<PhysicsCollisionEvent>(collisionEvent);
	}
}

void PhysicsSystem::syncPhysicsToWorld()
{
	auto entities = m_world->getEntitiesWith<Physics, Transform, PhysicsRuntime>();

	for (auto entity : entities) {
		auto& trans = entity.getComponent<Transform>();
		auto& physics = entity.getComponent<Physics>();
		auto& runtime = entity.getComponent<PhysicsRuntime>();

		// ========================================
		// 情况1：角色控制器
		// ========================================
		if (physics.isCharacter && runtime.ghostObject) {
			btTransform bulletTrans = runtime.ghostObject->getWorldTransform();
			trans.position = BulletToGlm(bulletTrans.getOrigin());
			//trans.rotation = BulletToGlm(bulletTrans.getRotation());
		}
		// ========================================
		// 情况2：刚体
		// ========================================
		else if (runtime.body && runtime.motionState) {
			btTransform bulletTrans;
			runtime.body->getMotionState()->getWorldTransform(bulletTrans);
			trans.position = BulletToGlm(bulletTrans.getOrigin());

			// 如果是 Dynamic 且不固定旋转，同步旋转
			if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation) {
				trans.rotation = BulletToGlm(bulletTrans.getRotation());
			}
		}
	}
}

// 创建物理体
void PhysicsSystem::createBody(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime)
{
	runtime.world = phyWorld;
	runtime.debugRevision = ++m_bodyRevision;

	if (physics.isCharacter) {
		runtime.shapePtr = createShape(*physics.collisionShape.children[0]);
		runtime.shapePtr->setLocalScaling(GlmToBullet(trans.scale));
		createCharacter(entity, physics, trans, runtime);
	}
	else {
		runtime.shapePtr = createCompoundShape(physics.collisionShape.children, runtime);
		runtime.shapePtr->setLocalScaling(GlmToBullet(trans.scale));
		if (runtime.shapePtr->getShapeType() == COMPOUND_SHAPE_PROXYTYPE)
		{
			// 优化组合体，重新计算 AABB，提升碰撞检测性能
			static_cast<btCompoundShape*>(runtime.shapePtr)->recalculateLocalAabb();
		}
		createRigidBody(entity, physics, trans, runtime);
	}
}

void PhysicsSystem::createRigidBody(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime)
{
	float mass = physics.mass;
	btVector3 localInertia(0, 0, 0);

	if (physics.bodyType == Physics::BodyType::Static || physics.bodyType == Physics::BodyType::Kinematic)
		mass = 0.f;

	if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation)
		runtime.shapePtr->calculateLocalInertia(mass, localInertia);

	btTransform startTransform;
	startTransform.setIdentity();
	startTransform.setOrigin(GlmToBullet(trans.position));
	startTransform.setRotation(GlmToBullet(trans.rotation));
	runtime.motionState = new btDefaultMotionState(startTransform);

	btRigidBody* body = new btRigidBody(mass, runtime.motionState, runtime.shapePtr, localInertia);
	body->setFriction(physics.friction);
	body->setRestitution(physics.restitution);
	body->setRollingFriction(physics.rollingFriction);
	body->setUserPointer(new Entity(entity));
	body->setMassProps(mass, localInertia);
	body->updateInertiaTensor();

	if (physics.bodyType == Physics::BodyType::Kinematic)
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
	if (physics.isSensor)
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	if (!physics.allowSleep || physics.bodyType == Physics::BodyType::Kinematic)
		body->setActivationState(DISABLE_DEACTIVATION);
	if (physics.isBullet) {
		body->setCcdMotionThreshold(1.f);

		btTransform trans;
		trans.setIdentity();
		btVector3 aabbmin, aabbmax;
		runtime.shapePtr->getAabb(trans, aabbmin, aabbmax);
		btVector3 aabbsize = aabbmax - aabbmin;
		float maxSize = std::max(aabbsize.x(), std::max(aabbsize.y(), aabbsize.z()));
		body->setCcdSweptSphereRadius(maxSize * 0.5f);
	}

	phyWorld->addRigidBody(body);
	runtime.body = body;
}

void PhysicsSystem::createCharacter(Entity entity, Physics& physics, Transform& trans, PhysicsRuntime& runtime)
{
	if (!runtime.shapePtr)
		return;

	btConvexShape* convexShape = nullptr;
	// 检查形状类型是否是凸体
	int shapeType = runtime.shapePtr->getShapeType();
	if (shapeType == BOX_SHAPE_PROXYTYPE ||
		shapeType == SPHERE_SHAPE_PROXYTYPE ||
		shapeType == CAPSULE_SHAPE_PROXYTYPE ||
		shapeType == CYLINDER_SHAPE_PROXYTYPE ||
		shapeType == CONVEX_HULL_SHAPE_PROXYTYPE) {
		// 安全转换
		convexShape = static_cast<btConvexShape*>(runtime.shapePtr);
	}
	else {
		// 非凸体，回退到胶囊体
		std::cerr << "Warning: Character shape must be convex, falling back to Capsule" << std::endl;
		return;
	}

	// 1. 创建幽灵物体 (Ghost Object)
	btPairCachingGhostObject* ghost = new btPairCachingGhostObject();
	ghost->setCollisionShape(convexShape);
	ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

	if (physics.bodyType == Physics::BodyType::Kinematic)
		ghost->setCollisionFlags(ghost->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);

	if (physics.isSensor)
		ghost->setCollisionFlags(ghost->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

	if (!physics.allowSleep || physics.bodyType == Physics::BodyType::Kinematic)
		ghost->setActivationState(DISABLE_DEACTIVATION);

	btTransform startTransform;
	startTransform.setIdentity();
	startTransform.setOrigin(GlmToBullet(trans.position));
	startTransform.setRotation(GlmToBullet(trans.rotation));
	ghost->setWorldTransform(startTransform);

	runtime.ghostObject = ghost;

	// 2. 创建角色控制器
	btKinematicCharacterController* character = new btKinematicCharacterController(
		ghost,
		convexShape,
		physics.stepHeight
	);

	// 设置参数
	character->setJumpSpeed(physics.jumpSpeed);
	character->setFallSpeed(-phyWorld->getGravity().y() * 4.f);
	character->setMaxSlope(glm::radians(physics.maxSlope));
	character->setGravity(phyWorld->getGravity());
	character->setMaxPenetrationDepth(physics.maxPenetrationDepth);

	runtime.character = character;

	// 3. 添加到物理世界
	// 先添加到碰撞对象（用于碰撞检测）
	phyWorld->addCollisionObject(
		ghost,
		btBroadphaseProxy::CharacterFilter,                       // 自身碰撞组
		btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter  // 与哪些组碰撞
	);

	// 再添加为 Action（用于物理更新）
	phyWorld->addAction(character);

	// 存储 Entity 指针用于碰撞回调
	ghost->setUserPointer(new Entity(entity));
}

void PhysicsSystem::updateRigidBody(Physics& physics, PhysicsRuntime& runtime)
{
	if (!runtime.body || !runtime.shapePtr)
		return;

	phyWorld->removeRigidBody(runtime.body);

	float mass = physics.mass;
	btVector3 localInertia(0, 0, 0);

	if (physics.bodyType == Physics::BodyType::Static || physics.bodyType == Physics::BodyType::Kinematic)
		mass = 0.f;

	if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation)
		runtime.shapePtr->calculateLocalInertia(mass, localInertia);

	auto& body = runtime.body;

	body->setFriction(physics.friction);
	body->setRestitution(physics.restitution);
	body->setRollingFriction(physics.rollingFriction);
	body->setMassProps(mass, localInertia);
	body->updateInertiaTensor();

	auto flags = mass > 0.f ? btCollisionObject::CF_DYNAMIC_OBJECT : btCollisionObject::CF_STATIC_OBJECT;
	body->setCollisionFlags(flags);

	if (physics.bodyType == Physics::BodyType::Kinematic)
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
	if (physics.isSensor)
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	if (!physics.allowSleep || physics.bodyType == Physics::BodyType::Kinematic)
		body->setActivationState(DISABLE_DEACTIVATION);
	else
		body->setActivationState(ACTIVE_TAG);

	body->activate(true);

	if (physics.isBullet) {
		body->setCcdMotionThreshold(1.f);

		btTransform trans;
		trans.setIdentity();
		btVector3 aabbmin, aabbmax;
		runtime.shapePtr->getAabb(trans, aabbmin, aabbmax);
		btVector3 aabbsize = aabbmax - aabbmin;
		float maxSize = std::max(aabbsize.x(), std::max(aabbsize.y(), aabbsize.z()));
		body->setCcdSweptSphereRadius(maxSize * 0.5f);
	}
	else
	{
		body->setCcdMotionThreshold(0.f);
		body->setCcdSweptSphereRadius(0.f);
	}

	body->setLinearVelocity(btVector3(0, 0, 0));
	body->setAngularVelocity(btVector3(0, 0, 0));

	phyWorld->addRigidBody(runtime.body);
}

void PhysicsSystem::updateCharacter(Physics& physics, PhysicsRuntime& runtime)
{

	auto& character = runtime.character;
	auto& ghost = runtime.ghostObject;

	phyWorld->removeAction(character);

	ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);
	if (physics.bodyType == Physics::BodyType::Kinematic)
		ghost->setCollisionFlags(ghost->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
	if (physics.isSensor)
		ghost->setCollisionFlags(ghost->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	if (!physics.allowSleep || physics.bodyType == Physics::BodyType::Kinematic)
		ghost->setActivationState(DISABLE_DEACTIVATION);
	else
		ghost->setActivationState(ACTIVE_TAG);

	ghost->activate(true);

	character->setStepHeight(physics.stepHeight);
	character->setJumpSpeed(physics.jumpSpeed);
	character->setFallSpeed(-phyWorld->getGravity().y() * 4.f);
	character->setMaxSlope(glm::radians(physics.maxSlope));
	character->setGravity(phyWorld->getGravity());
	character->setMaxPenetrationDepth(physics.maxPenetrationDepth);

	phyWorld->addAction(character);
}

// ======================== 碰撞体调试显示 ========================

namespace
{
	glm::mat4 ToGlm(const btTransform& t)
	{
		const btMatrix3x3& basis = t.getBasis();
		const btVector3& origin = t.getOrigin();
		glm::mat4 m(1.0f);
		for (int c = 0; c < 3; ++c)
			m[c] = glm::vec4(basis[0][c], basis[1][c], basis[2][c], 0.0f);		// btMatrix3x3::operator[] 取的是行
		m[3] = glm::vec4(origin.x(), origin.y(), origin.z(), 1.0f);
		return m;
	}

	// 收集 Bullet 调试绘制的线段。debugDrawObject 里的盒子、球、胶囊、圆柱等最后都走 drawLine。
	// local 不为空：写一个物体局部空间的线框（缓存用）——顶点按位置焊接，同一个碰撞形状里重复的边只留一条
	// （三角网格相邻三角形共享的边、球面 / 胶囊重复画的弧都在这里去掉）；否则写 out.lines（世界坐标的 AABB / 接触点）
	class DebugLineCollector : public btIDebugDraw
	{
	public:
		explicit DebugLineCollector(PhysicsDebugLines& out) : _out(out) {}

		uint32_t object = UINT32_MAX;
		uint16_t child = UINT16_MAX;
		PhysicsDebugLineKind kind = PhysicsDebugLineKind::Aabb;
		bool usedCamera = false;			// 生成的线段和相机位置有关（三角网格只取了相机周围）

		void BeginLocal(PhysicsDebugShapeLines* lines)
		{
			_local = lines;
			_vertexIndex.clear();
			_edgeSet.clear();
			_edgeChild = UINT16_MAX;
			usedCamera = false;
		}
		void EndLocal()
		{
			_local = nullptr;
			_vertexIndex.clear();
			_edgeSet.clear();
		}

		void Add(const btVector3& a, const btVector3& b)
		{
			if (!_local)
			{
				_out.lines.push_back({ BulletToGlm(a), BulletToGlm(b), object, child, kind });
				return;
			}
			const uint32_t ia = Weld(a);
			const uint32_t ib = Weld(b);
			if (ia == ib)
				return;			// 退化的边
			// 边去重只在同一个碰撞形状里做：不同形状的边分开记，属性面板悬停高亮才能按形状区分
			if (child != _edgeChild)
			{
				_edgeSet.clear();
				_edgeChild = child;
			}
			const uint64_t key = ia < ib ? (static_cast<uint64_t>(ia) << 32 | ib) : (static_cast<uint64_t>(ib) << 32 | ia);
			if (!_edgeSet.insert(key).second)
				return;
			_local->edges.push_back({ ia, ib, child });
		}

		void drawLine(const btVector3& from, const btVector3& to, const btVector3& /*color*/) override
		{
			Add(from, to);
		}

		// 接触点：沿法线的短线 + 一个小十字
		void drawContactPoint(const btVector3& pointOnB, const btVector3& normalOnB, btScalar /*distance*/, int /*lifeTime*/, const btVector3& /*color*/) override
		{
			constexpr btScalar kNormalLength = 0.25f;
			constexpr btScalar kCross = 0.04f;
			Add(pointOnB, pointOnB + normalOnB * kNormalLength);
			Add(pointOnB - btVector3(kCross, 0, 0), pointOnB + btVector3(kCross, 0, 0));
			Add(pointOnB - btVector3(0, kCross, 0), pointOnB + btVector3(0, kCross, 0));
			Add(pointOnB - btVector3(0, 0, kCross), pointOnB + btVector3(0, 0, kCross));
		}

		void reportErrorWarning(const char* /*warningString*/) override {}
		void draw3dText(const btVector3& /*location*/, const char* /*textString*/) override {}
		void setDebugMode(int debugMode) override { _mode = debugMode; }
		int getDebugMode() const override { return _mode; }

	private:
		// 位置逐位相同的顶点合成一个（共享顶点经过同一个变换，结果逐位相同）；-0 和 +0 当成同一个
		using PointKey = std::array<uint32_t, 3>;
		struct PointKeyHash
		{
			size_t operator()(const PointKey& k) const noexcept
			{
				size_t h = 1469598103934665603ull;
				for (uint32_t v : k)
					h = (h ^ v) * 1099511628211ull;
				return h;
			}
		};

		uint32_t Weld(const btVector3& p)
		{
			const glm::vec3 v = BulletToGlm(p);
			PointKey key{};
			for (int i = 0; i < 3; ++i)
			{
				const float f = v[i] == 0.0f ? 0.0f : v[i];
				std::memcpy(&key[i], &f, sizeof(float));
			}
			const auto [it, inserted] = _vertexIndex.try_emplace(key, static_cast<uint32_t>(_local->vertices.size()));
			if (inserted)
				_local->vertices.push_back(v);
			return it->second;
		}

		PhysicsDebugLines& _out;
		PhysicsDebugShapeLines* _local = nullptr;
		std::unordered_map<PointKey, uint32_t, PointKeyHash> _vertexIndex;
		std::unordered_set<uint64_t> _edgeSet;
		uint16_t _edgeChild = UINT16_MAX;
		int _mode = DBG_DrawWireframe;
	};

	// 三角网格：只取查询包围盒里的三角形（Bullet 自己的 debugDrawObject 会遍历全部三角形）。
	// 共享的边、共享的顶点由 DebugLineCollector 去重
	class DebugTriangleCollector : public btTriangleCallback
	{
	public:
		DebugTriangleCollector(DebugLineCollector& lines, const btTransform& transform) : _lines(lines), _transform(transform) {}

		void processTriangle(btVector3* triangle, int /*partId*/, int /*triangleIndex*/) override
		{
			const btVector3 a = _transform * triangle[0];
			const btVector3 b = _transform * triangle[1];
			const btVector3 c = _transform * triangle[2];
			_lines.Add(a, b);
			_lines.Add(b, c);
			_lines.Add(c, a);
		}

	private:
		DebugLineCollector& _lines;
		const btTransform& _transform;
	};

	// 包围盒 8 个角都在视锥同一个侧面外面就算看不见（只看 x / y 和相机背后）
	bool AabbOutsideFrustum(const glm::mat4& viewProj, const btVector3& mn, const btVector3& mx, float margin)
	{
		int outside[5] = { 0, 0, 0, 0, 0 };
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec4 p((i & 1) ? mx.x() : mn.x(), (i & 2) ? mx.y() : mn.y(), (i & 4) ? mx.z() : mn.z(), 1.0f);
			const glm::vec4 c = viewProj * p;
			const float w = c.w * margin;
			if (c.x < -w) ++outside[0];
			if (c.x > w) ++outside[1];
			if (c.y < -w) ++outside[2];
			if (c.y > w) ++outside[3];
			if (c.w <= 0.0f) ++outside[4];
		}
		for (int count : outside)
		{
			if (count == 8)
				return true;
		}
		return false;
	}

	float DistanceToAabb(const btVector3& p, const btVector3& mn, const btVector3& mx)
	{
		const btVector3 q(std::clamp(p.x(), mn.x(), mx.x()), std::clamp(p.y(), mn.y(), mx.y()), std::clamp(p.z(), mn.z(), mx.z()));
		return (p - q).length();
	}

	// 凸包的边：btConvexHullShape 没有多面体特征时，Bullet 的 getEdge 只会把点按顺序连成折线，所以自己算。
	// 只在生成缓存时调用，不再单独缓存
	void DrawHullEdges(DebugLineCollector& lines, const btTransform& transform, const btConvexHullShape* hull)
	{
		const int n = hull->getNumPoints();
		if (n <= 0)
			return;
		btConvexHullComputer computer;
		// btVector3 是 4 个 btScalar（x, y, z, w），步长按整个 btVector3 算
		computer.compute(&hull->getUnscaledPoints()[0].x(), static_cast<int>(sizeof(btVector3)), n, 0.0f, 0.0f);
		const btVector3 scale = hull->getLocalScaling();
		for (int i = 0; i < computer.edges.size(); ++i)
		{
			// 每条边有正反两个方向，只取一个
			const auto& edge = computer.edges[i];
			const int a = edge.getSourceVertex();
			const int b = edge.getTargetVertex();
			if (a < b)
				lines.Add(transform * (computer.vertices[a] * scale), transform * (computer.vertices[b] * scale));
		}
	}

	// camera：相机在 transform 所在坐标系里的位置（生成缓存时是物体局部空间）
	void DrawShape(btCollisionWorld& world, DebugLineCollector& lines, const btTransform& transform, const btCollisionShape* shape,
		const btVector3& camera, float meshRadius)
	{
		if (!shape)
			return;

		// 复合形状：逐个子形状画（子形状的变换已经包含了 setLocalScaling 带来的位置缩放）
		if (shape->getShapeType() == COMPOUND_SHAPE_PROXYTYPE)
		{
			const auto* compound = static_cast<const btCompoundShape*>(shape);
			for (int i = 0; i < compound->getNumChildShapes(); ++i)
				DrawShape(world, lines, transform * compound->getChildTransform(i), compound->getChildShape(i), camera, meshRadius);
			return;
		}

		// 三角网格等凹形状：只查相机周围的三角形。BVH 建在缩放后的局部空间里，transform 不含缩放，所以半径不用换算
		if (shape->isConcave())
		{
			const auto* concave = static_cast<const btConcaveShape*>(shape);
			btVector3 aabbMin(-BT_LARGE_FLOAT, -BT_LARGE_FLOAT, -BT_LARGE_FLOAT);
			btVector3 aabbMax(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT);
			if (meshRadius > 0.0f)
			{
				const btVector3 local = transform.invXform(camera);
				const btVector3 r(meshRadius, meshRadius, meshRadius);
				aabbMin = local - r;
				aabbMax = local + r;
				lines.usedCamera = true;
			}
			DebugTriangleCollector callback(lines, transform);
			concave->processAllTriangles(&callback, aabbMin, aabbMax);
			return;
		}

		// 凸包：没有多面体特征时用自己算的边（不调 initializePolyhedralFeatures，免得改变碰撞的接触点生成）
		if (shape->getShapeType() == CONVEX_HULL_SHAPE_PROXYTYPE)
		{
			const auto* hull = static_cast<const btConvexHullShape*>(shape);
			if (!hull->getConvexPolyhedron())
			{
				DrawHullEdges(lines, transform, hull);
				return;
			}
		}

		world.debugDrawObject(transform, shape, btVector3(1, 1, 1));
	}
}

void PhysicsSystem::CollectDebugLines(const PhysicsDebugDrawOptions& options, PhysicsDebugLines& out)
{
	const auto startTime = std::chrono::steady_clock::now();
	out.objects.clear();
	out.lines.clear();
	out.shapeLineCount = 0;
	out.shapeVertexCount = 0;
	out.rebuiltObjects = 0;
	if (!phyWorld || !m_world)
		return;

	const uint64_t useCount = ++m_debugDrawCount;
	DebugLineCollector collector(out);
	btIDebugDraw* previousDrawer = phyWorld->getDebugDrawer();
	phyWorld->setDebugDrawer(&collector);		// debugDrawObject 内部直接用 getDebugDrawer()，不检查空指针

	auto wanted = [&](Entity entity)
		{
			return !options.filterByEntities
				|| std::binary_search(options.onlyEntities.begin(), options.onlyEntities.end(), entity.getId());
		};
	const btVector3 cameraPos = GlmToBullet(options.cameraPosition);
	const float meshRadius = options.triangleMeshRadius;

	for (Entity entity : m_world->getEntitiesWith<Physics, PhysicsRuntime>())
	{
		if (!wanted(entity))
			continue;
		const auto& physics = entity.getComponent<Physics>();
		const auto& runtime = entity.getComponent<PhysicsRuntime>();
		const btCollisionObject* object = runtime.body ? static_cast<const btCollisionObject*>(runtime.body)
			: static_cast<const btCollisionObject*>(runtime.ghostObject);
		if (!object || !object->getCollisionShape())
			continue;

		const btTransform& transform = object->getWorldTransform();
		const btCollisionShape* rootShape = object->getCollisionShape();
		btVector3 aabbMin, aabbMax;
		rootShape->getAabb(transform, aabbMin, aabbMax);
		if (options.maxDistance > 0.0f && DistanceToAabb(cameraPos, aabbMin, aabbMax) > options.maxDistance)
			continue;
		if (options.useFrustum && AabbOutsideFrustum(options.viewProj, aabbMin, aabbMax, options.frustumMargin))
			continue;

		// ---- 局部空间线段：有缓存且没过期就直接用 ----
		const btVector3 localCamera = transform.invXform(cameraPos);
		PhysicsDebugShapeCacheEntry& entry = m_debugShapeCache[entity.getId()];
		const bool stale = !entry.lines
			|| entry.revision != runtime.debugRevision
			|| entry.scaling != rootShape->getLocalScaling()
			|| (entry.cameraDependent && (entry.queryRadius != meshRadius || (localCamera - entry.queryCenter).length() > meshRadius * 0.25f));
		if (stale)
		{
			auto lines = std::make_shared<PhysicsDebugShapeLines>();
			collector.BeginLocal(lines.get());
			// 顶层复合形状的第 i 个子形状就是 Physics::collisionShape.children[i]（createCompoundShape 按顺序添加）；
			// 角色控制器不用复合形状，只有 children[0]
			if (rootShape->getShapeType() == COMPOUND_SHAPE_PROXYTYPE)
			{
				const auto* compound = static_cast<const btCompoundShape*>(rootShape);
				for (int i = 0; i < compound->getNumChildShapes(); ++i)
				{
					collector.child = static_cast<uint16_t>(std::min(i, UINT16_MAX - 1));
					DrawShape(*phyWorld, collector, compound->getChildTransform(i), compound->getChildShape(i), localCamera, meshRadius);
				}
			}
			else
			{
				collector.child = 0;
				DrawShape(*phyWorld, collector, btTransform::getIdentity(), rootShape, localCamera, meshRadius);
			}
			collector.EndLocal();
			collector.child = UINT16_MAX;
			lines->vertices.shrink_to_fit();
			lines->edges.shrink_to_fit();

			entry.revision = runtime.debugRevision;
			entry.scaling = rootShape->getLocalScaling();
			entry.cameraDependent = collector.usedCamera;
			entry.queryRadius = meshRadius;
			entry.queryCenter = localCamera;
			entry.lines = std::move(lines);
			++out.rebuiltObjects;
		}
		entry.lastUsed = useCount;

		const uint32_t objectIndex = static_cast<uint32_t>(out.objects.size());
		out.objects.push_back({ entity, physics.bodyType, physics.isSensor, physics.isCharacter,
			object->getActivationState() == ISLAND_SLEEPING, ToGlm(transform), entry.lines });
		out.shapeLineCount += entry.lines->edges.size();
		out.shapeVertexCount += entry.lines->vertices.size();

		if (options.drawAabb)
		{
			collector.object = objectIndex;
			collector.kind = PhysicsDebugLineKind::Aabb;
			collector.drawAabb(aabbMin, aabbMax, btVector3(1, 0, 0));
		}
	}

	if (options.drawContacts && phyWorld->getDispatcher())
	{
		collector.object = UINT32_MAX;
		collector.kind = PhysicsDebugLineKind::Contact;
		btDispatcher* dispatcher = phyWorld->getDispatcher();
		const int numManifolds = dispatcher->getNumManifolds();
		for (int i = 0; i < numManifolds; ++i)
		{
			const btPersistentManifold* manifold = dispatcher->getManifoldByIndexInternal(i);
			const int numContacts = manifold->getNumContacts();
			if (numContacts == 0)
				continue;
			if (options.filterByEntities)
			{
				const auto* a = static_cast<const Entity*>(manifold->getBody0()->getUserPointer());
				const auto* b = static_cast<const Entity*>(manifold->getBody1()->getUserPointer());
				if (!(a && wanted(*a)) && !(b && wanted(*b)))
					continue;
			}
			for (int j = 0; j < numContacts; ++j)
			{
				const btManifoldPoint& point = manifold->getContactPoint(j);
				collector.drawContactPoint(point.m_positionWorldOnB, point.m_normalWorldOnB, point.getDistance(), point.getLifeTime(), btVector3(1, 1, 0));
			}
		}
	}

	phyWorld->setDebugDrawer(previousDrawer);

	// 很久没画到的物体缓存清掉（实体可能已经删了；只按 id 比较）
	constexpr uint64_t kCacheKeep = 600;
	std::erase_if(m_debugShapeCache, [useCount](const auto& item) { return useCount - item.second.lastUsed > kCacheKeep; });

	out.collectMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - startTime).count();
}
