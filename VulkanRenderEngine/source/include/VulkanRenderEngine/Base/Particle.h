#pragma once

#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Effect.h"

enum class ParticleBlendMode { Additive = 0, AlphaBlend = 1 };

enum class ParticleFacing
{
	CameraPlane = 0,		// 正对相机平面：用相机的 right/up
	CameraPosition = 1,		// 朝向相机位置：用 center->camera 方向与推导出的正交基
	VelocityAligned = 2,	// 沿速度方向拉伸（拖尾/火花）
	AxisLocked = 3,			// 锁定世界轴（地面烟圈）
};

enum class ParticleMask
{
	None = 0,		// 不裁：方片
	Disc = 1,		// 硬边圆
	SoftDisc = 2,	// 径向羽化圆
};

// mesh 渲染器用的网格来源：内置立方体 / 内置球 / 用户自己的模型
enum class ParticleMeshType
{
	Cube = 0,
	Sphere = 1,
	Custom = 2,
};

struct ParticleRenderInfo
{
	bool mesh = false;										// false: billboard quad；true: 网格（内置立方体/球或模型）
	ParticleFacing facing = ParticleFacing::CameraPlane;	// 仅 billboard 生效
	ParticleMask mask = ParticleMask::Disc;					// 仅 billboard 生效
	ParticleMeshType meshType = ParticleMeshType::Sphere;	// 仅 mesh 生效
	std::shared_ptr<Material> material;						// billboard：可空（空 = 纯 tint 颜色）
	std::shared_ptr<StaticModel> model;						// 仅 mesh + Custom：用户模型

	bool HasMaterial() const { return material != nullptr; }
};

struct BaseParticleProperties :public BaseEffectProperties
{
	ParticleBlendMode blendMode = ParticleBlendMode::Additive;
	ParticleRenderInfo renderInfo;

	glm::vec3 baseColor = glm::vec3(1.0f);
	float opacity = 1.0f;

	glm::mat4 transform = glm::mat4(1.0f);
	glm::vec3 position = glm::vec3(0.f);
	glm::quat rotation = glm::identity<glm::quat>();
	glm::vec3 scale = glm::vec3(1.f);

public:
	BaseParticleProperties() { effectType = EffectType::Particle; }
	BaseParticleProperties(const glm::vec3& color, float opacity) :baseColor(color), opacity(std::clamp(opacity, 0.f, 1.f)) { effectType = EffectType::Particle; }
	BaseParticleProperties(const BaseParticleProperties& other)
		:blendMode(other.blendMode)
		, renderInfo(other.renderInfo)
		, baseColor(other.baseColor)
		, opacity(other.opacity)
		, transform(other.transform)
		, position(other.position)
		, rotation(other.rotation)
		, scale(other.scale) {
		effectType = EffectType::Particle;
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() const = 0;
};

struct ParticleProperties final :public BaseParticleProperties
{
	ParticleProperties() = default;
	ParticleProperties(const glm::vec3& color, float opacity) :BaseParticleProperties(color, opacity) {}
	ParticleProperties(const ParticleProperties& other) :BaseParticleProperties(other) {}

	virtual std::shared_ptr<BaseEffectProperties> Clone() const override {
		return std::make_shared<ParticleProperties>(*this);
	}
};
