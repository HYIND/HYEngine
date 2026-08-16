#include "Systems/PhysicsSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "Helper/Tools.h"
#include <algorithm>

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

btCollisionShape* createShape(Transform& trans, CollisionShape::ChildShape& childShape)
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

btCollisionShape* createCompoundShape(Transform& trans, std::vector<std::shared_ptr<CollisionShape::ChildShape>>& childShapes)
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
		btCollisionShape* childShape = createShape(trans, *child);

		// 计算局部变换
		btTransform localTransform;
		localTransform.setIdentity();
		localTransform.setOrigin(GlmToBullet(child->position));
		localTransform.setRotation(GlmToBullet(child->rotation));

		// 添加到组合体
		child->shapePtr = childShape;

		compoundShape->addChildShape(localTransform, childShape);
	}

	// 3. 优化组合体（可选）
	// 重新计算 AABB，提升碰撞检测性能
	compoundShape->recalculateLocalAabb();

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
	{
		auto entities = m_world->getEntitiesWith<Physics, Transform>();
		for (auto entity : entities)
		{
			if (!entity.hasComponent<TagPhysiscCreate>())
			{
				auto& physics = entity.getComponent<Physics>();
				auto& trans = entity.getComponent<Transform>();
				createBody(entity, physics, trans);
				entity.addComponent<TagPhysiscCreate>();
			}
		}
	}
}

void PhysicsSystem::syncWorldToPhysics(float dt_second)
{
	auto entities = m_world->getEntitiesWith<Physics, Transform>();

	for (auto entity : entities)
	{
		auto& physics = entity.getComponent<Physics>();
		auto* trans = entity.tryGetComponent<Transform>();
		if (!trans) continue;

		if (physics.isCharacter && physics.character)
		{
			if (physics.ghostObject)
			{
				btTransform currentTrans = physics.ghostObject->getWorldTransform();
				currentTrans.setRotation(GlmToBullet(trans->rotation));
				physics.ghostObject->setWorldTransform(currentTrans);
				if (!physics.ghostObject->isActive())
					physics.ghostObject->activate(true);
			}

			// 从 Movement 组件读取输入
			if (auto* move = entity.tryGetComponent<Movement>())
			{
				float stepDisplacement = physics.walkSpeed * fixedTimeStep;
				btVector3 walkDir = GlmToBullet(trans->rotation * move->currentMoveDirection * stepDisplacement);
				physics.character->setWalkDirection(walkDir);

				// 跳跃
				if (move->canJump && move->currentWantJump && physics.character->canJump())
					physics.character->jump();
			}
		}

		if (physics.forceSyncTransform || physics.bodyType == Physics::BodyType::Kinematic)
		{
			btTransform newTrans;
			newTrans.setIdentity();
			newTrans.setOrigin(GlmToBullet(trans->position));
			newTrans.setRotation(GlmToBullet(trans->rotation));

			if (physics.body)
			{
				physics.body->setWorldTransform(newTrans);
				physics.body->getMotionState()->setWorldTransform(newTrans);
				physics.body->setLinearVelocity(btVector3(0, 0, 0));
				physics.body->setAngularVelocity(btVector3(0, 0, 0));
			}
			if (physics.ghostObject)
			{
				physics.ghostObject->setWorldTransform(newTrans);
			}
			physics.forceSyncTransform = false;
		}

		auto& shape = physics.collisionShape.shapePtr;
		if (shape)
		{
			if (shape->getLocalScaling() != GlmToBullet(trans->scale))
			{
				btVector3 newScale = GlmToBullet(trans->scale);
				shape->setLocalScaling(newScale);

				// Dynamic 物体需要重新计算惯性
				if (physics.bodyType == Physics::BodyType::Dynamic)
				{
					if (physics.body)
					{
						float mass = physics.mass;
						btVector3 localInertia(0, 0, 0);
						shape->calculateLocalInertia(mass, localInertia);
						physics.body->setMassProps(mass, localInertia);
						physics.body->updateInertiaTensor();
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
			auto clearShapeState = [&](Physics& physics)
				{
					if (physics.motionState)
					{
						delete physics.motionState;
						physics.motionState = nullptr;
					}
					if (physics.collisionShape.shapePtr)
					{
						delete physics.collisionShape.shapePtr;
						physics.collisionShape.shapePtr = nullptr;
					}
				};
			auto clearCharacter = [&](Physics& physics)
				{
					if (physics.character || physics.ghostObject)
					{
						if (physics.character)
						{
							phyWorld->removeAction(physics.character);
							delete physics.character;
							physics.character = nullptr;
						}
						if (physics.ghostObject)
						{
							phyWorld->removeCollisionObject(physics.ghostObject);
							delete physics.ghostObject;
							physics.ghostObject = nullptr;
						}
						clearShapeState(physics);
					}
				};
			auto clearRigidBody = [&](Physics& physics)
				{
					if (physics.body)
					{
						phyWorld->removeRigidBody(physics.body);
						delete physics.body;
						physics.body = nullptr;
						clearShapeState(physics);
					}
				};

			if (!physics.isCharacter)
			{
				if (physics.character || physics.ghostObject)
					clearCharacter(physics);

				if (physics.body)
					updateRigidBody(physics);
				else
					createBody(entity, physics, *trans);
			}
			else
			{
				if (physics.body)
					clearRigidBody(physics);

				if (physics.character && physics.ghostObject)
					updateCharacter(physics);
				else
					createBody(entity, physics, *trans);
			}

			physics.forceRecalculate = false;
		}

	}
}

void PhysicsSystem::collisionCheck(float dt)
{
	float dt_second = dt / 1000.f;

	syncWorldToPhysics(dt_second);

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
	auto entities = m_world->getEntitiesWith<Physics, Transform>();

	for (auto entity : entities) {
		auto& trans = entity.getComponent<Transform>();
		auto& physics = entity.getComponent<Physics>();

		// ========================================
		// 情况1：角色控制器
		// ========================================
		if (physics.isCharacter && physics.ghostObject) {
			btTransform bulletTrans = physics.ghostObject->getWorldTransform();
			trans.position = BulletToGlm(bulletTrans.getOrigin());
			//trans.rotation = BulletToGlm(bulletTrans.getRotation());
		}
		// ========================================
		// 情况2：刚体
		// ========================================
		else if (physics.body && physics.motionState) {
			btTransform bulletTrans;
			physics.body->getMotionState()->getWorldTransform(bulletTrans);
			trans.position = BulletToGlm(bulletTrans.getOrigin());

			// 如果是 Dynamic 且不固定旋转，同步旋转
			if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation) {
				trans.rotation = BulletToGlm(bulletTrans.getRotation());
			}
		}
	}
}

// 创建物理体
void PhysicsSystem::createBody(Entity entity, Physics& physics, Transform& trans)
{
	physics.world = phyWorld;

	if (physics.isCharacter) {
		if (physics.collisionShape.children.empty())
			return;
		physics.collisionShape.shapePtr = createShape(trans, *physics.collisionShape.children[0]);
		createCharacter(entity, physics, trans);
	}
	else {
		physics.collisionShape.shapePtr = createCompoundShape(trans, physics.collisionShape.children);
		createRigidBody(entity, physics, trans);
	}

	physics.collisionShape.shapePtr->setLocalScaling(GlmToBullet(trans.scale));
}

void PhysicsSystem::createRigidBody(Entity entity, Physics& physics, Transform& trans)
{
	float mass = physics.mass;
	btVector3 localInertia(0, 0, 0);

	if (physics.bodyType == Physics::BodyType::Static || physics.bodyType == Physics::BodyType::Kinematic)
		mass = 0.f;

	if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation)
		physics.collisionShape.shapePtr->calculateLocalInertia(mass, localInertia);

	btTransform startTransform;
	startTransform.setIdentity();
	startTransform.setOrigin(GlmToBullet(trans.position));
	startTransform.setRotation(GlmToBullet(trans.rotation));
	physics.motionState = new btDefaultMotionState(startTransform);

	btRigidBody* body = new btRigidBody(mass, physics.motionState, physics.collisionShape.shapePtr, localInertia);
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
		physics.collisionShape.shapePtr->getAabb(trans, aabbmin, aabbmax);
		btVector3 aabbsize = aabbmax - aabbmin;
		float maxSize = std::max(aabbsize.x(), std::max(aabbsize.y(), aabbsize.z()));
		body->setCcdSweptSphereRadius(maxSize * 0.5f);
	}

	phyWorld->addRigidBody(body);
	physics.body = body;
}

void PhysicsSystem::createCharacter(Entity entity, Physics& physics, Transform& trans)
{
	if (!physics.collisionShape.shapePtr)
		return;

	btConvexShape* convexShape = nullptr;
	// 检查形状类型是否是凸体
	int shapeType = physics.collisionShape.shapePtr->getShapeType();
	if (shapeType == BOX_SHAPE_PROXYTYPE ||
		shapeType == SPHERE_SHAPE_PROXYTYPE ||
		shapeType == CAPSULE_SHAPE_PROXYTYPE ||
		shapeType == CYLINDER_SHAPE_PROXYTYPE ||
		shapeType == CONVEX_HULL_SHAPE_PROXYTYPE) {
		// 安全转换
		convexShape = static_cast<btConvexShape*>(physics.collisionShape.shapePtr);
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

	physics.ghostObject = ghost;

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

	physics.character = character;

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

void PhysicsSystem::updateRigidBody(Physics& physics)
{
	if (!physics.body || !physics.collisionShape.shapePtr)
		return;

	phyWorld->removeRigidBody(physics.body);

	float mass = physics.mass;
	btVector3 localInertia(0, 0, 0);

	if (physics.bodyType == Physics::BodyType::Static || physics.bodyType == Physics::BodyType::Kinematic)
		mass = 0.f;

	if (physics.bodyType == Physics::BodyType::Dynamic && !physics.fixedRotation)
		physics.collisionShape.shapePtr->calculateLocalInertia(mass, localInertia);

	auto& body = physics.body;

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
		physics.collisionShape.shapePtr->getAabb(trans, aabbmin, aabbmax);
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

	phyWorld->addRigidBody(physics.body);
}

void PhysicsSystem::updateCharacter(Physics& physics)
{

	auto& character = physics.character;
	auto& ghost = physics.ghostObject;

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
