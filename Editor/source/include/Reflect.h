#pragma once

#include "ECSCore/World.h"
#include "ECSCore/Entity.h"

#include "rfl.hpp"
#include "rfl/fields.hpp"

#include <algorithm>
#include <cmath>

#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"

#include "VulkanRenderEngine/RenderOption.h"

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
	struct Reflector<CameraComponent> {

		struct CameraData {
			glm::vec3 worldUp;
			float fov;
			float nearPlane;
			float farPlane;
		};

		using SrcType = CameraComponent;
		using ReflType = CameraData;

		static ReflType from(const SrcType& src) noexcept {
			return { src.worldUp, src.fov, src.nearPlane , src.farPlane };
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.worldUp = ref.worldUp;
			src.fov = ref.fov;
			src.nearPlane = ref.nearPlane;
			src.farPlane = ref.farPlane;
		}
	};

	template <>
	struct Reflector<DirectionalLightData> {

		struct Data {
			glm::vec3 color = glm::vec3(1.0f);

			bool volumeLightEnable = true;
			bool volumeLightEffectByAtmospher = false;
			float volumetricScatteringIntensity = 1.0f;

			bool castShadow = true;
			uint32_t shadowMapSize = __LightDataDefault::__default_shadow_side;

			float luxIntensity = 3.5f;

			uint32_t cascadeLevel = __LightDataDefault::__default_cascadeLevel;	//阴影级联
			float shadowDistance = __LightDataDefault::__default_shadowDistance;			// 阴影距离（米）
			float cascadeSplitLambda = __LightDataDefault::__default_cascadeSplitLambda;	// 级联切分比例 0 均匀 ~ 1 对数
		};

		using SrcType = DirectionalLightData;
		using ReflType = Data;

		static ReflType from(const SrcType& src) noexcept {
			return Data{
				.color = src.color,
				.volumeLightEnable = src.volumeLightEnable,
				.volumeLightEffectByAtmospher = src.volumeLightEffectByAtmospher,
				.volumetricScatteringIntensity = src.volumetricScatteringIntensity,
				.castShadow = src.castShadow,
				.shadowMapSize = src.shadowMapSize,
				.luxIntensity = src.luxIntensity,
				.cascadeLevel = src.cascadeLevel,
				.shadowDistance = src.shadowDistance,
				.cascadeSplitLambda = src.cascadeSplitLambda
			};
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.color = ref.color;
			src.volumeLightEnable = ref.volumeLightEnable;
			src.volumeLightEnable = ref.volumeLightEnable;
			src.volumeLightEffectByAtmospher = ref.volumeLightEffectByAtmospher;
			src.volumetricScatteringIntensity = ref.volumetricScatteringIntensity;
			src.castShadow = ref.castShadow;
			src.shadowMapSize = ref.shadowMapSize;
			src.luxIntensity = ref.luxIntensity;
			src.cascadeLevel = ref.cascadeLevel;
			src.shadowDistance = std::max(ref.shadowDistance, 1.0f);
			src.cascadeSplitLambda = std::clamp(ref.cascadeSplitLambda, 0.0f, 1.0f);
		}
	};

	template <>
	struct Reflector<PointLightData> {

		struct Data {
			glm::vec3 color = glm::vec3(1.0f);

			bool volumeLightEnable = true;
			bool volumeLightEffectByAtmospher = false;
			float volumetricScatteringIntensity = 1.0f;

			bool castShadow = true;
			uint32_t shadowMapSize = __LightDataDefault::__default_shadow_side;

			float cdIntensity = 300.f;
		};

		using SrcType = PointLightData;
		using ReflType = Data;

		static ReflType from(const SrcType& src) noexcept {
			return Data{
				.color = src.color,
				.volumeLightEnable = src.volumeLightEnable,
				.volumeLightEffectByAtmospher = src.volumeLightEffectByAtmospher,
				.volumetricScatteringIntensity = src.volumetricScatteringIntensity,
				.castShadow = src.castShadow,
				.shadowMapSize = src.shadowMapSize,
				.cdIntensity = src.cdIntensity
			};
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.color = ref.color;
			src.volumeLightEnable = ref.volumeLightEnable;
			src.volumeLightEffectByAtmospher = ref.volumeLightEffectByAtmospher;
			src.volumetricScatteringIntensity = ref.volumetricScatteringIntensity;
			src.castShadow = ref.castShadow;
			src.shadowMapSize = ref.shadowMapSize;
			src.cdIntensity = ref.cdIntensity;
		}
	};

	template <>
	struct Reflector<SpotLightData> {

		struct Data {
			glm::vec3 color = glm::vec3(1.0f);

			bool volumeLightEnable = true;
			bool volumeLightEffectByAtmospher = false;
			float volumetricScatteringIntensity = 1.0f;

			bool castShadow = true;
			uint32_t shadowMapSize = __LightDataDefault::__default_shadow_side;

			float cdIntensity = 300.f;

			float cutOffAngle = 15.f;
			float outercutOffAngle = 30.f;
		};

		using SrcType = SpotLightData;
		using ReflType = Data;

		static ReflType from(const SrcType& src) noexcept {
			return Data{
				.color = src.color,
				.volumeLightEnable = src.volumeLightEnable,
				.volumeLightEffectByAtmospher = src.volumeLightEffectByAtmospher,
				.volumetricScatteringIntensity = src.volumetricScatteringIntensity,
				.castShadow = src.castShadow,
				.shadowMapSize = src.shadowMapSize,
				.cdIntensity = src.cdIntensity,
				.cutOffAngle = src.cutOffAngle,
				.outercutOffAngle = src.outercutOffAngle
			};
		}

		static void modify(SrcType& src, const ReflType& ref) noexcept {
			src.color = ref.color;
			src.volumeLightEnable = ref.volumeLightEnable;
			src.volumeLightEffectByAtmospher = ref.volumeLightEffectByAtmospher;
			src.volumetricScatteringIntensity = ref.volumetricScatteringIntensity;
			src.castShadow = ref.castShadow;
			src.shadowMapSize = ref.shadowMapSize;
			src.cdIntensity = ref.cdIntensity;
			src.cutOffAngle = ref.cutOffAngle;
			src.outercutOffAngle = ref.outercutOffAngle;
		}
	};

    template <>
    struct Reflector<CameraFollow> {

        struct Data {
            EntityID targetID;
            glm::vec3 offset = glm::vec3(-1.0f, 3.0f, -3.0f);
            float smoothSpeed = 5.0f;
        };

        using SrcType = CameraFollow;
        using ReflType = Data;

        static ReflType from(const SrcType& src) noexcept {
            return Data{
                .targetID = src.target.getId(),
                .offset = src.offset,
                .smoothSpeed = src.smoothSpeed
            };
        }

        static void modify(SrcType& src, const ReflType& ref) noexcept {
            src.offset = ref.offset;
            src.smoothSpeed = ref.smoothSpeed;
        }
    };

    // DDGI 探针全局光照参数。面板是手写的（PropertiesHelper.inl 的 DrawComponent 特化），
    // 这里的反射只用于通用路径；uvec3 在反射层用 vec3 承载，写回时取整并夹到合法范围。
    template <>
    struct Reflector<Scene3DRenderOption::DDGIParams> {

        struct DDGIParamsRefl {
            bool cascadeEnable = true;
            uint32_t cascadeLevels = 4;
            glm::vec3 cascadeBaseSpacing = glm::vec3(1.0f);
            glm::vec3 cascadeCounts = glm::vec3(24.0f, 12.0f, 24.0f);
            float cascadeFadeCells = 2.0f;
            bool cascadeAutoRelocate = true;

            float normalBias = 0.1f;
            float viewBias = 0.10f;
            float maxRayDistance = 15.0f;
            float backfaceThreshold = 2.0f;
            float skyIntensity = 1.0f;
            float energy = 1.0f;

            uint32_t updateBudget = 8192;
            float hysteresis = 0.9f;
            float lightChangeThreshold = 0.0f;

            static DDGIParamsRefl formSrc(const Scene3DRenderOption::DDGIParams& src)
            {
                return DDGIParamsRefl{
                    .cascadeEnable = src.cascadeEnable,
                    .cascadeLevels = src.cascadeLevels,
                    .cascadeBaseSpacing = src.cascadeBaseSpacing,
                    .cascadeCounts = glm::vec3(src.cascadeCounts),
                    .cascadeFadeCells = src.cascadeFadeCells,
                    .cascadeAutoRelocate = src.cascadeAutoRelocate,
                    .normalBias = src.normalBias,
                    .viewBias = src.viewBias,
                    .maxRayDistance = src.maxRayDistance,
                    .backfaceThreshold = src.backfaceThreshold,
                    .skyIntensity = src.skyIntensity,
                    .energy = src.energy,
                    .updateBudget = src.updateBudget,
                    .hysteresis = src.hysteresis,
                    .lightChangeThreshold = src.lightChangeThreshold
                };
            }

            void modify(Scene3DRenderOption::DDGIParams& src) const
            {
                src.cascadeEnable = cascadeEnable;
                src.cascadeLevels = std::clamp<uint32_t>(cascadeLevels, 1u, 4u);
                src.cascadeBaseSpacing = glm::max(cascadeBaseSpacing, glm::vec3(0.25f));
                src.cascadeCounts = glm::uvec3(glm::clamp(glm::round(cascadeCounts), glm::vec3(1.0f), glm::vec3(255.0f)));
                src.cascadeFadeCells = std::max(0.01f, cascadeFadeCells);
                src.cascadeAutoRelocate = cascadeAutoRelocate;

                src.normalBias = std::max(0.0f, normalBias);
                src.viewBias = std::max(0.0f, viewBias);
                src.maxRayDistance = std::max(0.1f, maxRayDistance);
                src.backfaceThreshold = std::max(0.001f, backfaceThreshold);
                src.skyIntensity = std::max(0.0f, skyIntensity);
                src.energy = std::max(0.0f, energy);

                src.updateBudget = std::max(1u, updateBudget);
                src.hysteresis = std::clamp(hysteresis, 0.0f, 0.98f);
                src.lightChangeThreshold = std::max(0.0f, lightChangeThreshold);
            }
        };

        using SrcType = Scene3DRenderOption::DDGIParams;
        using ReflType = DDGIParamsRefl;

        static ReflType from(const SrcType& src) noexcept {
            return ReflType::formSrc(src);
        }
        static void modify(SrcType& src, const ReflType& ref) noexcept {
            ref.modify(src);
        }
    };
}

enum class PropertyWidget
{
	Default,
	Color,
    Const
};

template<typename T>
struct PropertyUI
{
	static PropertyWidget GetWidget(std::string_view)
	{
		return PropertyWidget::Default;
	}
};

template<>
struct PropertyUI<Scene3DRenderOption::ExponentialHeightFogParams>
{
	static PropertyWidget GetWidget(std::string_view name)
	{
		if (name == "fogAlbedo")
			return PropertyWidget::Color;
		return PropertyWidget::Default;
	}
};

template<>
struct PropertyUI<Scene3DRenderOption::VolumetricCloudParams>
{
	static PropertyWidget GetWidget(std::string_view name)
	{
		if (name == "cloudAlbedo" || name == "cloudAmbientColor")
			return PropertyWidget::Color;
		return PropertyWidget::Default;
	}
};

template<>
struct PropertyUI<CameraFollow>
{
	static PropertyWidget GetWidget(std::string_view name)
	{
		if (name == "targetID")
			return PropertyWidget::Const;
		return PropertyWidget::Default;
	}
};
