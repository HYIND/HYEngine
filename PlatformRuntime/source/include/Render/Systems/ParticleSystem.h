#pragma once

#include "ECSCore/System.h"
#include "ECSCore/Types.h"
#include "Render/Components/ParticleEmitter.h"
#include <memory>
#include <unordered_map>
#include <vector>

class ParticleSystem :public System
{
public:
	ParticleSystem();
	virtual void update(float deltaTime) override;

	// ===== 供渲染器使用 =====
	// 最近一次 update 结束时生成的只读渲染数据（没有可渲染的粒子时为 nullptr）。
	// 数据生成后不再修改，渲染侧可以跨线程、跨帧持有
	std::shared_ptr<const ParticleFrameData> GetFrameData() const;
	std::size_t GetParticleCount() const;

private:
	// 一个发射器发出的粒子，按发射顺序存放；压缩时保持相对顺序，所以渲染参数相同的粒子是连续的
	struct EmitterParticles
	{
		std::vector<ParticleState> particles;
		bool visible = true;	// 跟随 ParticleEmitter::visible
		bool seen = false;		// 本帧发射器实体还在。发射器删掉后，已发射的粒子继续活到寿命结束
	};

	void UpdateParticle(float deltaTime);
	void EmitParticle(float deltaTime);
	// 按发射速率 / 突发计算本帧要发射的数量，按 budget（这个发射器剩余的上限）截断；
	// 截掉的部分直接丢弃，不留到下一帧补发
	static std::size_t ComputeEmitCount(ParticleEmitter& emitter, float deltaSecond, std::size_t budget);
	static void InitParticle(ParticleState& particle, const Transform& transform, const glm::mat4& emitterMatrix, const EmitterParams& params);
	void BuildFrameData();

private:
	std::unordered_map<EntityID, EmitterParticles> _emitters;
	std::size_t _particleCount = 0;

	std::shared_ptr<const ParticleFrameData> _frameData;
};

HY_DECLARE_ECS_TYPE(ParticleSystem, "hy.engine.system.Particle");
