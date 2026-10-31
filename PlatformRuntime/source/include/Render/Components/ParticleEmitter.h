
#pragma once

#include "ECSCore/IComponent.h"
#include "VulkanRenderEngine/Base/Particle.h"
#include "CommonComponent.h"
#include "Helper/Tools.h"
#include "glm/gtc/random.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <variant>
#include <vector>

struct IOpacityUpdater;
struct IBaseColorUpdater;
struct IScaleUpdater;
struct IRotationUpdater;
struct IAccelerationUpdater;
struct IAccelerationUpdater;

struct Particle :public Renderable
{
	glm::vec3 position = glm::vec3(0.f);
	glm::quat rotation = glm::identity<glm::quat>();
	glm::vec3 scale = glm::vec3(1.0f);

	glm::vec3 velocity = glm::vec3(0.f);

	std::shared_ptr<IOpacityUpdater> opacityUpdater;
	std::shared_ptr<IBaseColorUpdater> baseColorUpdater;
	std::shared_ptr<IScaleUpdater> scaleUpdater;

	std::shared_ptr<IRotationUpdater> rotationUpdater;
	std::shared_ptr<IAccelerationUpdater> localAccelerationUpdater;
	std::shared_ptr<IAccelerationUpdater> worldAccelerationUpdater;

	float lifeTime;
	float maxLifeTime;

	std::shared_ptr<BaseParticleProperties> properties;

	void InitUpdater();

	virtual void OnRemove(const Entity& e) {
		opacityUpdater.reset();
		baseColorUpdater.reset();
		scaleUpdater.reset();
		rotationUpdater.reset();
		localAccelerationUpdater.reset();
		worldAccelerationUpdater.reset();
		properties.reset();
	}
};

// Color生成器接口
struct IScaleGenerator {
	virtual glm::vec3 generate() const = 0;
	virtual std::shared_ptr<IScaleGenerator> Clone() const = 0;
	virtual ~IScaleGenerator() = default;
};

struct RandomScaleGenerator : IScaleGenerator {
	glm::vec3 min, max;
	RandomScaleGenerator(const glm::vec3& min, const glm::vec3& max) :min(min), max(max) {}
	glm::vec3 generate() const override { return Tool::RandomLinear(min, max); }
	std::shared_ptr<IScaleGenerator> Clone() const override { return std::make_shared<RandomScaleGenerator>(min, max); }
};

struct FixedScaleGenerator : IScaleGenerator {
	glm::vec3 scale;
	FixedScaleGenerator(const glm::vec3& scale) :scale(scale) {}
	glm::vec3 generate() const override { return scale; }
	std::shared_ptr<IScaleGenerator> Clone() const override { return std::make_shared<FixedScaleGenerator>(scale); }
};

// Color生成器接口
struct IColorGenerator {
	virtual glm::vec3 generate() const = 0;
	virtual std::shared_ptr<IColorGenerator> Clone() const = 0;
	virtual ~IColorGenerator() = default;
};

struct RandomColorGenerator : IColorGenerator {
	glm::vec3 min, max;
	RandomColorGenerator(const glm::vec3& min, const glm::vec3& max) :min(min), max(max) {}
	glm::vec3 generate() const override { return Tool::RandomLinear(min, max); }
	std::shared_ptr<IColorGenerator> Clone() const override { return std::make_shared<RandomColorGenerator>(min, max); }
};

struct FixedColorGenerator : IColorGenerator {
	glm::vec3 color;
	FixedColorGenerator(const glm::vec3& color) :color(color) {}
	glm::vec3 generate() const override { return color; }
	std::shared_ptr<IColorGenerator> Clone() const override { return std::make_shared<FixedColorGenerator>(color); }
};

// lifetime生成器接口
struct ILifeTimeGenerator {
	virtual float generate() const = 0;
	virtual std::shared_ptr<ILifeTimeGenerator> Clone() const = 0;
	virtual ~ILifeTimeGenerator() = default;
};

struct RandomLifeTimeGenerator : ILifeTimeGenerator {
	float min, max;
	RandomLifeTimeGenerator(float min, float max) :min(min), max(max) {}
	float generate() const override { return Tool::RandomLinear(min, max); }
	std::shared_ptr<ILifeTimeGenerator> Clone() const override { return std::make_shared<RandomLifeTimeGenerator>(min, max); }
};

struct FixedLifeTimeGenerator : ILifeTimeGenerator {
	float lifetime;
	FixedLifeTimeGenerator(float lifetime) :lifetime(lifetime) {}
	float generate() const override { return lifetime; }
	std::shared_ptr<ILifeTimeGenerator> Clone() const override { return std::make_shared<FixedLifeTimeGenerator>(lifetime); }
};

// Aplha生成器接口
struct IOpacityGenerator {
	virtual float generate() const = 0;
	virtual std::shared_ptr<IOpacityGenerator> Clone() const = 0;
	virtual ~IOpacityGenerator() = default;
};

struct RandomOpacityGenerator : IOpacityGenerator {
	float min, max;
	RandomOpacityGenerator(float min, float max) :min(min), max(max) {}
	float generate() const override { return Tool::RandomLinear(min, max); }
	std::shared_ptr<IOpacityGenerator> Clone() const override { return std::make_shared<RandomOpacityGenerator>(min, max); }
};

struct FixedOpactiyGenerator : IOpacityGenerator {
	float opacity;
	FixedOpactiyGenerator(float opacity) :opacity(opacity) {}
	float generate() const override { return opacity; }
	std::shared_ptr<IOpacityGenerator> Clone() const override { return std::make_shared<FixedOpactiyGenerator>(opacity); }
};

// 位置生成器接口
struct IPositionGenerator {
	virtual glm::vec3 generate() const = 0;
	virtual std::shared_ptr<IPositionGenerator> Clone() const = 0;
	virtual ~IPositionGenerator() = default;
};

struct PointGenerator : IPositionGenerator {
	glm::vec3 generate() const override { return glm::vec3(0.0f); }
	std::shared_ptr<IPositionGenerator> Clone() const override { return std::make_shared<PointGenerator>(); }
};

struct SphericalGenerator : IPositionGenerator {
	float radius;
	SphericalGenerator(float radius = 1.0f) :radius(radius) {}
	glm::vec3 generate() const override {
		return radius * glm::sphericalRand(1.0f);
	}
	std::shared_ptr<IPositionGenerator> Clone() const override { return std::make_shared<SphericalGenerator>(radius); }
};

struct BallGenerator : IPositionGenerator {
	float radius;
	BallGenerator(float radius = 1.0f) :radius(radius) {}
	glm::vec3 generate() const override {
		return radius * glm::ballRand(1.0f);
	}
	std::shared_ptr<IPositionGenerator> Clone() const override { return std::make_shared<BallGenerator>(radius); }
};

struct BoxGenerator : IPositionGenerator {
	glm::vec3 halfSize;
	BoxGenerator(glm::vec3 halfSize = glm::vec3(1.0f)) :halfSize(halfSize) {}
	glm::vec3 generate() const override {
		return glm::vec3(
			Tool::RandomLinear(-halfSize.x, halfSize.x),
			Tool::RandomLinear(-halfSize.y, halfSize.y),
			Tool::RandomLinear(-halfSize.z, halfSize.z)
		);
	}
	std::shared_ptr<IPositionGenerator> Clone() const override { return std::make_shared<BoxGenerator>(halfSize); }
};

// 方向生成器接口
struct IDirectionGenerator {
	virtual glm::vec3 generate(const glm::vec3& pos, const Transform& emitterTrans) const = 0;
	virtual std::shared_ptr<IDirectionGenerator> Clone() const = 0;
	virtual ~IDirectionGenerator() = default;
};

struct FixedDirectionConeGenerator : IDirectionGenerator {
	glm::vec3 direction;
	float spreadAngle;
	FixedDirectionConeGenerator(const glm::vec3& direction, float spreadAngle) :direction(glm::normalize(direction)), spreadAngle(spreadAngle) {}
	glm::vec3 generate(const glm::vec3& pos, const Transform& emitterTrans) const override {
		glm::quat baseQuat = Tool::SafeQuatLookAt(direction);
		return glm::normalize(Tool::RandomSpread(baseQuat, spreadAngle) * glm::vec3(0, 0, -1));
	}
	std::shared_ptr<IDirectionGenerator> Clone() const override { return std::make_shared<FixedDirectionConeGenerator>(direction, spreadAngle); }
};

struct FollowEmitterDirectionConeGenerator : IDirectionGenerator {
	float spreadAngle;
	FollowEmitterDirectionConeGenerator(float spreadAngle) : spreadAngle(spreadAngle) {}
	glm::vec3 generate(const glm::vec3& pos, const Transform& emitterTrans) const override {
		return glm::normalize(Tool::RandomSpread(emitterTrans.rotation, spreadAngle) * glm::vec3(0, 0, -1));
	}
	std::shared_ptr<IDirectionGenerator> Clone() const override { return std::make_shared<FollowEmitterDirectionConeGenerator>(spreadAngle); }
};

struct RadialDirectionGenerator : IDirectionGenerator {
	float spreadAngle;
	RadialDirectionGenerator(float spreadAngle) :spreadAngle(spreadAngle) {}
	glm::vec3 generate(const glm::vec3& pos, const Transform& emitterTrans) const override {
		if (glm::length2(pos) == 0.f)
			return glm::normalize(Tool::RandomSpread(emitterTrans.rotation, spreadAngle) * glm::vec3(0, 0, -1));	//退化成发射器方向
		glm::vec3 radial = glm::normalize(pos);
		glm::quat baseQuat = Tool::SafeQuatLookAt(pos);
		return emitterTrans.GetMatrix() * glm::vec4(glm::normalize(Tool::RandomSpread(baseQuat, spreadAngle) * glm::vec3(0, 0, -1)), 0.f);
	}
	std::shared_ptr<IDirectionGenerator> Clone() const override { return std::make_shared<RadialDirectionGenerator>(spreadAngle); }
};

struct IRotationGenerator {
	virtual glm::quat generate(const glm::vec3& position, const glm::vec3& direction, const Transform& emitterTrans) const = 0;
	virtual std::shared_ptr<IRotationGenerator> Clone() const = 0;
	virtual ~IRotationGenerator() = default;
};

struct FixedRotationGenerator : IRotationGenerator {
	glm::quat rotation;
	FixedRotationGenerator(const glm::quat& q) :rotation(q) {}
	glm::quat generate(const glm::vec3&, const glm::vec3&, const Transform& emitterTrans) const override {
		return rotation;
	}
	std::shared_ptr<IRotationGenerator> Clone() const override { return std::make_shared<FixedRotationGenerator>(rotation); }
};

struct FollowEmitterRotationGenerator : IRotationGenerator {
	glm::quat generate(const glm::vec3&, const glm::vec3& direction, const Transform& emitterTrans) const override {
		return emitterTrans.rotation;
	}
	std::shared_ptr<IRotationGenerator> Clone() const override { return std::make_shared<FollowEmitterRotationGenerator>(); }
};

struct FollowVelocityRotationGenerator : IRotationGenerator {
	glm::quat generate(const glm::vec3&, const glm::vec3& direction, const Transform& emitterTrans) const override {
		if (glm::length2(direction) > 0.f)
			return Tool::SafeQuatLookAt(glm::normalize(direction));
		return glm::identity<glm::quat>();
	}
	std::shared_ptr<IRotationGenerator> Clone() const override { return std::make_shared<FollowVelocityRotationGenerator>(); }
};

// Aplha生成器接口
struct ISpeedGenerator {
	virtual float generate() const = 0;
	virtual std::shared_ptr<ISpeedGenerator> Clone() const = 0;
	virtual ~ISpeedGenerator() = default;
};

struct RandomSpeedGenerator : ISpeedGenerator {
	float min, max;
	RandomSpeedGenerator(float min, float max) :min(min), max(max) {}
	float generate() const override { return Tool::RandomLinear(min, max); }
	std::shared_ptr<ISpeedGenerator> Clone() const override { return std::make_shared<RandomSpeedGenerator>(min, max); }
};

struct FixedSpeedGenerator : ISpeedGenerator {
	float speed;
	FixedSpeedGenerator(float speed) :speed(speed) {}
	float generate() const override { return speed; }
	std::shared_ptr<ISpeedGenerator> Clone() const override { return std::make_shared<FixedSpeedGenerator>(speed); }
};

struct IPositionOffsetGenerator {
	virtual glm::vec3 generate(Particle* particle) const = 0;
	virtual std::shared_ptr<IPositionOffsetGenerator> Clone() const = 0;
	virtual ~IPositionOffsetGenerator() = default;
};

struct FixedPositionOffsetGenerator :public IPositionOffsetGenerator {
	glm::vec3 offset;
	FixedPositionOffsetGenerator(const glm::vec3& offset) :offset(offset) {}
	glm::vec3 generate(Particle* particle) const override { return offset; }
	std::shared_ptr<IPositionOffsetGenerator> Clone() const override { return std::make_shared<FixedPositionOffsetGenerator>(offset); }
};

struct RadialPositionOffsetGenerator :public IPositionOffsetGenerator {
	RadialPositionOffsetGenerator() {}
	glm::vec3 generate(Particle* particle) const override { return ((glm::length(particle->velocity) * glm::length(particle->velocity)) / 60.f) * glm::vec3(1, 0, 0); }
	std::shared_ptr<IPositionOffsetGenerator> Clone() const override { return std::make_shared<RadialPositionOffsetGenerator>(); }
};


struct IRotationUpdater
{
	virtual std::shared_ptr<IRotationUpdater> Clone() = 0;
	virtual void Init(Particle* particle) {};
	virtual glm::quat update(Particle* particle, float deltaTime) = 0;
	virtual ~IRotationUpdater() = default;
};

struct FollowVelocityRotationUpdater :public IRotationUpdater
{
	std::shared_ptr<IRotationUpdater> Clone() override { return std::make_shared<FollowVelocityRotationUpdater>(); }
	glm::quat update(Particle* part, float deltaTime) override {
		if (glm::length2(part->velocity) > 0.f)
			return Tool::SafeQuatLookAt(glm::normalize(part->velocity));
		return part->rotation;
	}
};

struct IAccelerationUpdater {
	virtual std::shared_ptr<IAccelerationUpdater> Clone() = 0;
	virtual void Init(Particle* particle) {};
	virtual glm::vec3 update(Particle*, float deltaTime) = 0;
	virtual ~IAccelerationUpdater() = default;
};

struct NoneAccelerationUpdater :public IAccelerationUpdater
{
	NoneAccelerationUpdater() {}
	std::shared_ptr<IAccelerationUpdater> Clone() override { return std::make_shared<NoneAccelerationUpdater>(); }
	glm::vec3 update(Particle* part, float deltaTime) override { return glm::vec3(0.f); }
};

struct FiexedAccelerationUpdater :public IAccelerationUpdater
{
	glm::vec3 acceleration = glm::vec3(0);
	FiexedAccelerationUpdater(const glm::vec3& acceleration) :acceleration(acceleration) {}
	std::shared_ptr<IAccelerationUpdater> Clone() override { return std::make_shared<FiexedAccelerationUpdater>(acceleration); }
	glm::vec3 update(Particle* part, float deltaTime) override { return acceleration; }
};

struct GravityAccelerationUpdater :public FiexedAccelerationUpdater
{
	GravityAccelerationUpdater() :FiexedAccelerationUpdater(glm::vec3(0, -9.8, 0)) {}
	std::shared_ptr<IAccelerationUpdater> Clone() override { return std::make_shared<GravityAccelerationUpdater>(); }
	glm::vec3 update(Particle* part, float deltaTime) override { return acceleration; }
};

struct LinearAccelerationUpdater :public IAccelerationUpdater
{
	glm::vec3 accelerationDir = glm::vec3(0);
	float initAccelerationValue;
	float goalAccelerationValue;
	LinearAccelerationUpdater(const glm::vec3& accelerationDir, float initAccelerationValue, float goalAccelerationValue)
		:accelerationDir(glm::normalize(accelerationDir)), initAccelerationValue(initAccelerationValue), goalAccelerationValue(goalAccelerationValue) {}
	std::shared_ptr<IAccelerationUpdater> Clone() override { return std::make_shared<LinearAccelerationUpdater>(accelerationDir, initAccelerationValue, goalAccelerationValue); }
	glm::vec3 update(Particle* part, float deltaTime) override {
		float t = 0.f;
		if (part->maxLifeTime > 0.f && part->lifeTime > 0.f)
			t = part->lifeTime / part->maxLifeTime;

		return accelerationDir * Tool::LinearLerp(initAccelerationValue, goalAccelerationValue, 1.0 - t);
	}
};

struct PerlinTurbulenceUpdater : public IAccelerationUpdater
{
	float strength;
	float frequency;
	float time;
	glm::vec3 offset;  // 每个粒子的随机偏移

	PerlinTurbulenceUpdater(float strength = 0.5, float frequency = 1.5f)
		: strength(strength), frequency(frequency), time(0.0f) {
		offset = glm::vec3(
			Tool::RandomLinear(0.0f, 100.0f),
			Tool::RandomLinear(0.0f, 100.0f),
			Tool::RandomLinear(0.0f, 100.0f)
		);
	}

	std::shared_ptr<IAccelerationUpdater> Clone() override {
		return std::make_shared<PerlinTurbulenceUpdater>(strength, frequency);
	}

	// 简化的3D噪声函数（组合正弦波）
	float fbm(glm::vec3 p) {
		float value = 0.0f;
		float amplitude = 0.5f;
		float frequency2 = 1.0f;
		for (int i = 0; i < 3; i++) {
			value += amplitude * sin(p.x * frequency2 + p.y * frequency2 * 0.7f + p.z * frequency2 * 1.3f);
			amplitude *= 0.5f;
			frequency2 *= 2.0f;
		}
		return value;
	}

	glm::vec3 update(Particle* particle, float deltaTime) override {
		time += deltaTime;

		// 使用位置和时间的3D噪声
		glm::vec3 noisePos = particle->position * frequency + glm::vec3(time * 1.5f) + offset;

		float noiseX = fbm(noisePos);
		float noiseY = fbm(noisePos + glm::vec3(100.0f, 0.0f, 0.0f));
		float noiseZ = fbm(noisePos + glm::vec3(0.0f, 100.0f, 0.0f));

		// 主流扰动在水平方向，垂直方向有轻微扰动
		return glm::vec3(
			noiseX * strength,
			0.65 + noiseY * strength * 0.2f,  // 垂直扰动小一些
			noiseZ * strength
		);
	}
};

struct IOpacityUpdater {
	virtual std::shared_ptr<IOpacityUpdater> Clone() = 0;
	virtual void Init(Particle* particle) {};
	virtual float update(Particle*, float deltaTime) = 0;
	virtual ~IOpacityUpdater() = default;
};

struct LinearOpacityUpdater :public IOpacityUpdater
{
	float initOpacity;
	float goalOpacity;
	LinearOpacityUpdater(float goalOpacity) :goalOpacity(goalOpacity) {}
	std::shared_ptr<IOpacityUpdater> Clone() override { return std::make_shared<LinearOpacityUpdater>(goalOpacity); }
	void Init(Particle* particle) override { initOpacity = particle->properties->opacity; }
	float update(Particle* particle, float deltaTime) override {
		float t = 0.f;
		if (particle->maxLifeTime > 0.f && particle->lifeTime > 0.f)
			t = particle->lifeTime / particle->maxLifeTime;

		return Tool::LinearLerp(initOpacity, goalOpacity, 1.0 - t);
	}
};

struct IBaseColorUpdater {
	virtual std::shared_ptr<IBaseColorUpdater> Clone() = 0;
	virtual void Init(Particle* particle) {};
	virtual glm::vec3 update(Particle*, float deltaTime) = 0;
	virtual ~IBaseColorUpdater() = default;
};

struct LinearBaseColorUpdater :public IBaseColorUpdater
{
	glm::vec3 initColor;
	glm::vec3 goalColor;
	LinearBaseColorUpdater(const glm::vec3& goalColor) :goalColor(goalColor) {}
	std::shared_ptr<IBaseColorUpdater> Clone() override { return std::make_shared<LinearBaseColorUpdater>(goalColor); }
	void Init(Particle* particle) override { initColor = particle->properties->baseColor; }
	glm::vec3 update(Particle* particle, float deltaTime) override {
		float t = 0.f;
		if (particle->maxLifeTime > 0.f && particle->lifeTime > 0.f)
			t = particle->lifeTime / particle->maxLifeTime;

		return Tool::LinearLerp(initColor, goalColor, 1.0 - t);
	}
};

struct FireColorUpdater : public IBaseColorUpdater
{
	glm::vec3 innerColor;   // 中心颜色（暗红）
	glm::vec3 midColor;     // 中间颜色（亮橙）
	glm::vec3 outerColor;   // 边缘颜色（黄白）

	FireColorUpdater(
		const glm::vec3& inner = glm::vec3(1.0f, 0.8f, 0.2f),		// 黄白
		const glm::vec3& mid = glm::vec3(0.7f, 0.3f, 0.05f),		// 亮橙
		const glm::vec3& outer = glm::vec3(0.1f, 0.0f, 0.0f)		// 暗红
	) : innerColor(inner), midColor(mid), outerColor(outer) {}

	std::shared_ptr<IBaseColorUpdater> Clone() override {
		return std::make_shared<FireColorUpdater>(innerColor, midColor, outerColor);
	}

	glm::vec3 update(Particle* particle, float deltaTime) override {
		float t = 0.f;
		if (particle->maxLifeTime > 0.f && particle->lifeTime > 0.f)
			t = particle->lifeTime / particle->maxLifeTime;
		else
			return glm::vec3(0, 1, 0);

		glm::vec3 color;
		if (t <= 0.3f) {
			float p = t / 0.3f;
			color = Tool::LinearLerp(innerColor, midColor, p);
		}
		else {
			float p = (t - 0.3f) / 0.7f;
			color = Tool::LinearLerp(midColor, outerColor, p);
		}

		return color;
	}
};

struct IScaleUpdater {
	virtual std::shared_ptr<IScaleUpdater> Clone() = 0;
	virtual void Init(Particle* particle) {};
	virtual glm::vec3 update(Particle*, float deltaTime) = 0;
	virtual ~IScaleUpdater() = default;
};

struct LinearScaleUpdater :public IScaleUpdater
{
	glm::vec3 initScale;
	glm::vec3 goalScale;
	LinearScaleUpdater(const glm::vec3& goalScale) :goalScale(goalScale) {}
	std::shared_ptr<IScaleUpdater> Clone() override { return std::make_shared<LinearScaleUpdater>(goalScale); }
	void Init(Particle* particle) override { initScale = particle->scale; }
	glm::vec3 update(Particle* particle, float deltaTime) override {
		float t = 0.f;
		if (particle->maxLifeTime > 0.f && particle->lifeTime > 0.f)
			t = particle->lifeTime / particle->maxLifeTime;

		return Tool::LinearLerp(initScale, goalScale, 1.0 - t);
	}
};

// ============================================================
//  渲染器
// ============================================================
// 一个发射器只选一种渲染器：
//   Billboard —— 内置 quad，圆形/软边在片元里裁；材质可空（空 = 纯 baseColor 颜色）。
//   Mesh      —— 模型渲染
struct ParticleBillboardRenderer
{
	ParticleFacing facing = ParticleFacing::CameraPlane;
	ParticleMask mask = ParticleMask::Disc;

	std::shared_ptr<Material> material;
	AssetGUID materialGuid;
};

struct ParticleMeshRenderer
{
	ParticleMeshType meshType = ParticleMeshType::Sphere;	// Cube / Sphere 用内置网格，Custom 用下面的 model

	std::shared_ptr<StaticModel> model;		// 仅 Custom：用户自己的模型
	AssetGUID modelGuid;

	std::shared_ptr<Material> material;
	AssetGUID materialGuid;
};

using ParticleRenderer = std::variant<ParticleBillboardRenderer, ParticleMeshRenderer>;

inline ParticleRenderInfo MakeParticleRenderInfo(const ParticleBillboardRenderer& renderer)
{
	ParticleRenderInfo info;
	info.mesh = false;
	info.facing = renderer.facing;
	info.mask = renderer.mask;
	info.material = renderer.material;
	return info;
}

inline ParticleRenderInfo MakeParticleRenderInfo(const ParticleMeshRenderer& renderer)
{
	ParticleRenderInfo info;
	info.mesh = true;
	info.mask = ParticleMask::None;
	info.meshType = renderer.meshType;

	if (renderer.meshType == ParticleMeshType::Custom)
	{
		info.model = renderer.model;
	}
	else
	{
		info.material = renderer.material;
	}

	return info;
}

inline ParticleRenderInfo MakeParticleRenderInfo(const ParticleRenderer& renderer)
{
	return std::visit([](const auto& r) { return MakeParticleRenderInfo(r); }, renderer);
}

template <typename T>
inline void CloneShared(const std::shared_ptr<T>& source, std::shared_ptr<T>& target)
{
	if (auto object = source)
		target = object->Clone();
	else
		target = nullptr;
}

// ============================================================
//  发射参数
// ============================================================
struct EmitterParams
{
	// ===== 发射速率 =====
	float emitRate = 30.0f;						// 每秒发射粒子数
	float emitRateJitter = 0.0f;				// 速率抖动比例（0~1）：本帧速率在 emitRate*(1±jitter) 内随机

	// ===== 外观参数 =====

	std::shared_ptr<IScaleGenerator> scaleGenerator;			//scale生成
	std::shared_ptr<IColorGenerator> colorGenerator;			//color生成
	std::shared_ptr<ILifeTimeGenerator> lifeTimeGenerator;		//lifetime生成
	std::shared_ptr<IOpacityGenerator> opacityGenerator;		//alpha生成

	// ===== 粒子发射物理 =====

	std::shared_ptr<IPositionGenerator>	posGenerator;	//粒子初始位置生成器
	std::shared_ptr<IDirectionGenerator> dirGenerator;	//粒子初始速度方向生成器
	std::shared_ptr<ISpeedGenerator> speedGenerator;	//粒子初始速度大小生成器
	std::shared_ptr<IRotationGenerator> rotGenerator;	//粒子初始朝向生成器

	std::shared_ptr<IPositionOffsetGenerator> posOffsetGenerator;	//局部坐标系偏移生成器

	// ===== 粒子更新 =====
	std::shared_ptr<IOpacityUpdater> opacityUpdater;
	std::shared_ptr<IBaseColorUpdater> baseColorUpdater;
	std::shared_ptr<IScaleUpdater> scaleUpdater;

	std::shared_ptr<IRotationUpdater> rotationUpdater;
	std::shared_ptr<IAccelerationUpdater> localAccelerationUpdater;
	std::shared_ptr<IAccelerationUpdater> worldAccelerationUpdater;

	// ===== 突发发射（Burst） =====
	int burstAmount = 0;                 // 一次性爆发粒子数（0=不使用）
	int burstCount = 0;					 // 总爆发次数，0=无限次数
	float burstInterval = 0.0f;          // 爆发间隔（秒），0=仅爆发一次

	// ===== 预热 =====
	float warmupTime = 0.0f;             // 预热时间（秒），让粒子系统提前发射

	ParticleBlendMode blendMode = ParticleBlendMode::Additive;
	ParticleRenderer renderer = ParticleBillboardRenderer{};

	EmitterParams() = default;
	EmitterParams(const EmitterParams& other) { CopyFrom(other); }
	EmitterParams& operator=(const EmitterParams& other)
	{
		if (this != &other)
			CopyFrom(other);
		return *this;
	}

	void CopyFrom(const EmitterParams& other)
	{
		emitRate = other.emitRate;
		emitRateJitter = other.emitRateJitter;

		CloneShared(other.scaleGenerator, scaleGenerator);
		CloneShared(other.colorGenerator, colorGenerator);
		CloneShared(other.lifeTimeGenerator, lifeTimeGenerator);
		CloneShared(other.opacityGenerator, opacityGenerator);

		CloneShared(other.posGenerator, posGenerator);
		CloneShared(other.dirGenerator, dirGenerator);
		CloneShared(other.speedGenerator, speedGenerator);
		CloneShared(other.rotGenerator, rotGenerator);
		CloneShared(other.posOffsetGenerator, posOffsetGenerator);

		CloneShared(other.opacityUpdater, opacityUpdater);
		CloneShared(other.baseColorUpdater, baseColorUpdater);
		CloneShared(other.scaleUpdater, scaleUpdater);
		CloneShared(other.rotationUpdater, rotationUpdater);
		CloneShared(other.localAccelerationUpdater, localAccelerationUpdater);
		CloneShared(other.worldAccelerationUpdater, worldAccelerationUpdater);

		burstAmount = other.burstAmount;
		burstCount = other.burstCount;
		burstInterval = other.burstInterval;
		warmupTime = other.warmupTime;

		blendMode = other.blendMode;
		renderer = other.renderer;
	}

	bool IsVaild() { return scaleGenerator && colorGenerator && lifeTimeGenerator && opacityGenerator && posGenerator && dirGenerator && speedGenerator && rotGenerator; }
};

inline void FillDefaultParticleEmitterParams(EmitterParams& params)
{
	params.emitRate = 30.0f;
	params.emitRateJitter = 0.0f;
	params.scaleGenerator = std::make_shared<FixedScaleGenerator>(glm::vec3(0.2f));
	params.colorGenerator = std::make_shared<FixedColorGenerator>(glm::vec3(1.0f));
	params.lifeTimeGenerator = std::make_shared<FixedLifeTimeGenerator>(3.0f);
	params.opacityGenerator = std::make_shared<FixedOpactiyGenerator>(1.0f);
	params.posGenerator = std::make_shared<PointGenerator>();
	params.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.3f);
	params.speedGenerator = std::make_shared<FixedSpeedGenerator>(5.0f);
	params.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>();
	params.posOffsetGenerator = std::make_shared<FixedPositionOffsetGenerator>(glm::vec3(0.0f));
}

struct EmitterState
{
	float emitAmountAccumulator = 0.f;

	int burstCountAccumulator = 0;
	float burstTimeAccumulator = 0.f;

	float timeAccumulator = 0.f;
};

struct ParticleEmitter :public IComponent
{
	bool enable = true;
	EmitterParams emitterParams;
	EmitterState emitterState;

	ParticleEmitter() {};

	ParticleEmitter(const ParticleEmitter& other)
		: enable(other.enable), emitterParams(other.emitterParams), emitterState(other.emitterState) {}

	ParticleEmitter& operator=(const ParticleEmitter& other)
	{
		if (this != &other)
		{
			enable = other.enable;
			emitterParams = other.emitterParams;
			emitterState = other.emitterState;
		}
		return *this;
	}

	virtual void OnRemove(const Entity& e) {
		emitterParams = EmitterParams();
		emitterState = EmitterState();
	}
};

HY_DECLARE_ECS_TYPE(Particle, "hy.engine.component.Particle");
HY_DECLARE_ECS_TYPE(ParticleEmitter, "hy.engine.component.ParticleEmitter");

inline void Particle::InitUpdater()
{
	if (opacityUpdater) opacityUpdater->Init(this);
	if (baseColorUpdater) baseColorUpdater->Init(this);
	if (scaleUpdater) scaleUpdater->Init(this);
	if (rotationUpdater) rotationUpdater->Init(this);
	if (localAccelerationUpdater) localAccelerationUpdater->Init(this);
	if (worldAccelerationUpdater) worldAccelerationUpdater->Init(this);
}
