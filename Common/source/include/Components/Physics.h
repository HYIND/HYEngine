#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include <iostream>
#include <string>
#include "ECSCore/IComponent.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "bullet/btBulletDynamicsCommon.h"
#include "bullet/BulletDynamics/Character/btKinematicCharacterController.h"
#include "bullet/BulletCollision/CollisionDispatch/btGhostObject.h"

// === 形状定义 ===
enum class ShapeType {
	Box,           // 立方体 (size = 半边长)
	Sphere,        // 球体 (size.x = 半径)
	Capsule,       // 胶囊体 (size.x = 半径, size.y = 高度)
	Cylinder,      // 圆柱体 (size.x = 半径, size.y = 高度)
	ConvexHull,    // 凸包 (从 vertices 生成)
	TriangleMesh   // 三角网格 (仅静态物体)
};

struct CollisionShape
{
	struct ChildShape
	{
		ShapeType shapeType = ShapeType::Box;
		glm::vec3 size = glm::vec3(0.5f);
		glm::vec3 position = glm::vec3(0.0f);
		glm::quat rotation = glm::identity<glm::quat>();
		std::vector<glm::vec3> vertices;  // 仅对ConvexHull和TriangleMesh有效
		std::vector<unsigned int> indices;  // 仅对TriangleMesh有效

		btCollisionShape* shapePtr = nullptr;

		~ChildShape()
		{
			if (shapePtr)
			{
				delete shapePtr;
				shapePtr = nullptr;
			}
		}
	};

	btCollisionShape* shapePtr = nullptr;
	std::vector<std::shared_ptr<ChildShape>> children;

	void AddBoxShape(const glm::vec3& size = glm::vec3(0.5f), glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::Box;
		shape->size = size;
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
	void AddSphereShape(float radius = 0.5f, glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		radius = std::max(0.f, radius);
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::Sphere;
		shape->size = glm::vec3(radius);
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
	void AddCapsuleShape(float radius = 0.5f, float height = 1.0f, glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		radius = std::max(0.f, radius);
		height = std::max(0.f, height);
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::Capsule;
		shape->size = glm::vec3(radius, height, radius);
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
	void AddCylinderShape(float radius = 0.5f, float height = 1.0f, glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		radius = std::max(0.f, radius);
		height = std::max(0.f, height);
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::Cylinder;
		shape->size = glm::vec3(radius, height / 2.f, radius);
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
	void AddConvexHullShape(const std::vector<glm::vec3>& vertices, glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::ConvexHull;
		shape->vertices = vertices;
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
	void AddTriangleMeshShape(const std::vector<glm::vec3>& vertices, const std::vector<unsigned int>& indices, glm::vec3 position = glm::vec3(0.0f), glm::quat rotation = glm::identity<glm::quat>())
	{
		auto shape = std::make_shared<ChildShape>();
		shape->shapeType = ShapeType::TriangleMesh;
		shape->vertices = vertices;
		shape->indices = indices;
		shape->position = position;
		shape->rotation = rotation;
		children.emplace_back(shape);
	}
};

struct Physics :public IComponent
{

	CollisionShape collisionShape;

	// === 物理类型 ===
	enum class BodyType {
		Static = 0,		// 质量 = 0，不动
		Dynamic,		// 质量 > 0，受物理影响
		Kinematic		// 质量 = 0，可手动控制
	};

	BodyType bodyType = BodyType::Dynamic;

	// === 物理属性 ===
	float mass = 1.0f;				// 质量
	float friction = 0.5f;			// 摩擦 0.0-1.0
	float rollingFriction = 0.5f;	// 摩擦 0.0-1.0
	float restitution = 0.3f;		// 弹性 0.0-1.0

	// === 高级选项 ===
	bool isSensor = false;		// 传感器模式（只检测不碰撞）
	bool fixedRotation = false; // 锁定旋转（俯视角游戏常用）
	bool isBullet = false;		// 连续碰撞检测（高速物体）

	// === 角色控制器专用字段 ===
	bool isCharacter = false;           // 是否为角色控制器
	float stepHeight = 1.8f;			// 台阶高度
	float walkSpeed = 20.0f;            // 移动速度
	float jumpSpeed = 7.5f;				// 跳跃速度
	float maxSlope = 30.0f;             // 最大爬坡角度（度）
	float maxPenetrationDepth = 0.2f;

	bool allowSleep = true;				//允许休眠

	// === 运行时数据 ===

	// ---- 刚体模式 ----
	btRigidBody* body = nullptr;
	btDefaultMotionState* motionState = nullptr;

	// ---- 角色模式 ----
	btKinematicCharacterController* character = nullptr;  // 角色控制器指针
	btPairCachingGhostObject* ghostObject = nullptr;      // 幽灵物体指针

	// ---- world ----
	btDiscreteDynamicsWorld* world = nullptr;

	// 标记下轮处理中强制同步Transform的位置
	bool forceSyncTransform = false;
	bool forceRecalculate = false;

	virtual void OnRemove(const Entity& e) override
	{
		if (world && body) {
			world->removeRigidBody(body);
		}
		if (world && character)
		{
			world->removeAction(character);
			delete character;
			character = nullptr;
		}
		if (world && ghostObject) {
			world->removeCollisionObject(ghostObject);
			delete ghostObject;
			ghostObject = nullptr;
		}

		if (motionState)
		{
			delete motionState;
			motionState = nullptr;
		}
		if (collisionShape.shapePtr)
		{
			delete collisionShape.shapePtr;
			collisionShape.shapePtr = nullptr;
		}
		if (body)
		{
			delete body;
			body = nullptr;
		}
	}
};