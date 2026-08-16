#pragma once

#include "ECSCore/World.h"
#include "ECSCore/Entity.h"

#include "rfl.hpp"
#include "rfl/fields.hpp"

#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"

namespace rfl {

	template <>
	struct Reflector<Transform> {

		struct TransformRefl {
			glm::vec3 position;
			glm::quat rotation;
			glm::vec3 scale;

			static TransformRefl formSrc(const Transform& src)
			{
				return TransformRefl{ src.position ,src.rotation,src.scale };
			}
			void modify(Transform& src) const
			{
				src.position = position;
				src.rotation = rotation;
				src.scale = glm::max(scale, 0.001f);
			}
		};

		using SrcType = Transform;
		using ReflType = TransformRefl;
		static ReflType from(const SrcType& src) noexcept {
			return ReflType::formSrc(src);
		}
		static void modify(SrcType& src, const ReflType& ref) noexcept {
			ref.modify(src);
		}
	};


	template <>
	struct Reflector<Physics> {

		struct PhysicsRefl
		{
			Physics::BodyType bodyType = Physics::BodyType::Dynamic;

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

			static PhysicsRefl formSrc(const Physics& src)
			{
				PhysicsRefl ref;
				ref.bodyType = src.bodyType;
				ref.mass = src.mass;
				ref.friction = src.friction;
				ref.rollingFriction = src.rollingFriction;
				ref.restitution = src.restitution;
				ref.isSensor = src.isSensor;
				ref.fixedRotation = src.fixedRotation;
				ref.isBullet = src.isBullet;
				ref.isCharacter = src.isCharacter;
				ref.stepHeight = src.stepHeight;
				ref.walkSpeed = src.walkSpeed;
				ref.jumpSpeed = src.jumpSpeed;
				ref.maxSlope = src.maxSlope;
				ref.maxPenetrationDepth = src.maxPenetrationDepth;
				ref.allowSleep = src.allowSleep;
				return ref;
			}
			void modify(Physics& src) const
			{
				src.bodyType = bodyType;
				src.mass = mass;
				src.friction = friction;
				src.rollingFriction = rollingFriction;
				src.restitution = restitution;
				src.isSensor = isSensor;
				src.fixedRotation = fixedRotation;
				src.isBullet = isBullet;
				src.isCharacter = isCharacter;
				src.stepHeight = stepHeight;
				src.walkSpeed = walkSpeed;
				src.jumpSpeed = jumpSpeed;
				src.maxSlope = maxSlope;
				src.maxPenetrationDepth = maxPenetrationDepth;
				src.allowSleep = allowSleep;
			}
		};

		using SrcType = Physics;
		using ReflType = PhysicsRefl;
		static ReflType from(const SrcType& src) noexcept {
			return ReflType::formSrc(src);
		}
		static void modify(SrcType& src, const ReflType& ref) noexcept {
			ref.modify(src);
		}
	};

	template <>
	struct Reflector<Movement> {

		struct MovementRefl
		{
			glm::vec3 currentMoveDirection = glm::vec3(0.f);
			bool currentWantJump = false;
			bool canJump = true;

			static MovementRefl formSrc(const Movement& src)
			{
				return MovementRefl{ src.currentMoveDirection,src.currentWantJump,src.canJump };
			}
			void modify(Movement& src) const
			{
				src.currentMoveDirection = currentMoveDirection;
				src.currentWantJump = currentWantJump;
				src.canJump = canJump;
			}
		};

		using SrcType = Movement;
		using ReflType = MovementRefl;
		static ReflType from(const SrcType& src) noexcept {
			return ReflType::formSrc(src);
		}
		static void modify(SrcType& src, const ReflType& ref) noexcept {
			ref.modify(src);
		}
	};

	template <>
	struct Reflector<VariableMaterial> {
		using SrcType = VariableMaterial;
		using ReflType = VariableMaterialData;

		static ReflType from(const SrcType& src) noexcept {
			return src.data;
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.SetAlbedo(ref.albedo);
			src.SetMetallic(ref.metallic);
			src.SetRoughness(ref.roughness);
			src.SetOpacity(ref.opacity);
			src.SetAlpahMode(ref.alphamode);
			src.SetTwoSide(ref.twosided);
		}
	};

	template <>
	struct Reflector<CameraComponent> {

		struct CameraData {
			float fov;
			float nearPlane;
			float farPlane;
		};

		using SrcType = CameraComponent;
		using ReflType = CameraData;

		static ReflType from(const SrcType& src) noexcept {
			return { src.camera.GetFOV(), src.camera.GetNearPlane(), src.camera.GetFarPlane() };
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.camera.SetFOV(ref.fov);
			src.camera.SetNearPlane(ref.nearPlane);
			src.camera.SetFarPlane(ref.farPlane);
		}
	};
}