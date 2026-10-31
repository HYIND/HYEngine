#pragma once

#include "ECSCore/System.h"
#include "Render/Components/ParticleEmitter.h"
#include "publicShare/ResourcePool.h"
#include <functional>
#include <vector>

class ParticlesPool :public ResPool<Particle>
{
public:
	ParticlesPool(uint32_t InitResNum, uint32_t maxResNum);
	virtual void ResetData(Particle* data);
};

class ParticleSystem :public System
{
public:
	ParticleSystem();
	virtual void update(float deltaTime) override;

	const std::vector<Particle*>& GetParticles() const;// ===== 供渲染器使用 =====

private:
	void UpdateParticle(float deltaTime);
	void EmitParticle(float deltaTime);

	void Emit(const Transform& transform, ParticleEmitter& emitter, float deltaSceond);
	void EmitTaskBody(const Transform& transform, const EmitterParams& params, std::size_t slot);


private:
	// ===== 粒子容器 =====
	std::vector<Particle*> _particles;     // 活动粒子
	int m_MaxParticles;

	ParticlesPool _particlesPool;

	// ===== 一帧的发射任务 =====
	std::vector<std::function<void()>> _emitTasks;
	std::vector<Particle*> _emitSlots;
	std::size_t _emitBudget = 0;            // 本帧新增粒子预算
};

HY_DECLARE_ECS_TYPE(ParticleSystem, "hy.engine.system.Particle");
