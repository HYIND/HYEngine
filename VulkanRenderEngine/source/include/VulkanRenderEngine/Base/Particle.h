#pragma once

#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Effect.h"
#include <limits>
#include <vector>

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

	// 能不能和另一个粒子画进同一个批次（网格 / 材质 / 模型 / 朝向 / 裁剪完全相同）
	bool SameBatch(const ParticleRenderInfo& o) const
	{
		return mesh == o.mesh && facing == o.facing && mask == o.mask && meshType == o.meshType
			&& material == o.material && model == o.model;
	}
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

// ============================================================
//  粒子系统每帧产出的渲染数据：整帧只读，由 ParticleSystem 生成、以 shared_ptr 交给渲染
// ============================================================

// 一个粒子在渲染侧需要的数据（按值连续存放）
struct ParticleRenderParticle
{
	glm::vec3 position = glm::vec3(0.0f);
	float opacity = 1.0f;
	glm::quat rotation = glm::identity<glm::quat>();
	glm::vec3 scale = glm::vec3(1.0f);
	glm::vec3 color = glm::vec3(1.0f);
};

// 一段渲染参数相同、在 particles 里连续的粒子。正常情况下一个发射器一个批次，
// 运行中改了发射器的网格 / 材质 / 混合模式后，新旧粒子共存的那段时间会切成多个批次
struct ParticleRenderBatch
{
	ParticleRenderInfo renderInfo;
	ParticleBlendMode blendMode = ParticleBlendMode::Additive;
	uint32_t firstParticle = 0;
	uint32_t particleCount = 0;

	// 剔除用：粒子中心的包围盒 + 最大尺寸（billboard 取 max(scale.x, scale.y)，mesh 取 scale 最大分量）。
	// 渲染侧乘上网格自身的包围半径再外扩，得到保守包围盒
	glm::vec3 centerMin = glm::vec3(std::numeric_limits<float>::max());
	glm::vec3 centerMax = glm::vec3(std::numeric_limits<float>::lowest());
	float maxExtent = 0.0f;
};

struct ParticleFrameData
{
	std::vector<ParticleRenderParticle> particles;
	std::vector<ParticleRenderBatch> batches;
};
