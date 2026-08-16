#pragma once

#include "ECSCore/System.h"
#include "Render/Components/ParticleEmitter.h"
#include "publicShare/ResourcePool.h"

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
	void Emit(const Transform& transform, ParticleEmitter& emitter, float deltaSceond);    // 发射器发出请求


private:
	// ===== 粒子容器 =====
	std::vector<Particle*> _particles;     // 活动粒子
	int m_MaxParticles;

	ParticlesPool _particlesPool;
};