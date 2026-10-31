#include "Render/Systems/ParticleSystem.h"
#include "CommonComponent.h"
#include "ECSCore/World.h"
#include "Helper/Tools.h"
#include <algorithm>
#include <execution>

namespace
{
	glm::mat4 MakeParticleTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), position)
			* glm::toMat4(rotation)
			* glm::scale(glm::mat4(1.0f), scale);
	}

	inline void AdvanceParticle(Particle& particle, float deltaTime, float deltaSecond)
	{
		BaseParticleProperties* properties = particle.properties.get();

		glm::vec3 localAccl(0.f);
		glm::vec3 worldAccl(0.f);
		if (auto updater = particle.opacityUpdater) properties->opacity = updater->update(&particle, deltaTime);
		if (auto updater = particle.baseColorUpdater) properties->baseColor = updater->update(&particle, deltaTime);
		if (auto updater = particle.scaleUpdater) particle.scale = updater->update(&particle, deltaTime);
		if (auto updater = particle.rotationUpdater) particle.rotation = updater->update(&particle, deltaTime);
		if (auto updater = particle.localAccelerationUpdater) localAccl = updater->update(&particle, deltaTime);
		if (auto updater = particle.worldAccelerationUpdater) worldAccl = updater->update(&particle, deltaTime);

		glm::vec3 accl = worldAccl;
		if (localAccl != glm::vec3(0.f))
			accl += particle.rotation * localAccl;

		particle.velocity += accl * deltaSecond;
		particle.position += particle.velocity * deltaSecond;

		properties->position = particle.position;
		properties->rotation = particle.rotation;
		properties->scale = particle.scale;
	}
}

ParticleSystem::ParticleSystem()
	:m_MaxParticles(50000), _particlesPool(3000, 10000)
{}

void ParticleSystem::update(float deltaTime)
{
	auto start = Tool::GetTimestampMircoseconds();
	UpdateParticle(deltaTime);
	std::cout << std::format("ParticleSystem upadte cost {}us, Particle number = {}\n", Tool::GetTimestampMircoseconds() - start, _particles.size());
	EmitParticle(deltaTime);
}

void ParticleSystem::UpdateParticle(float deltaTime)
{
	const float deltaSecond = deltaTime / 1000.f;

	std::for_each(std::execution::par_unseq, _particles.begin(), _particles.end(),
		[deltaTime, deltaSecond](Particle* particle)
		{
			particle->lifeTime -= deltaSecond;
			if (particle->lifeTime <= 0.f || !particle->properties)
				return;

			AdvanceParticle(*particle, deltaTime, deltaSecond);
		}
	);

	// 2) 一趟就地压缩：活着的按原顺序往前挤，死的当场归还池子。
	//    （原来 partition 之后还要再 par_unseq 遍历一遍尾部做回收。）
	std::size_t writeIndex = 0;
	const std::size_t count = _particles.size();
	for (std::size_t readIndex = 0; readIndex < count; ++readIndex)
	{
		Particle* particle = _particles[readIndex];
		if (particle && particle->lifeTime > 0.f)
		{
			_particles[writeIndex++] = particle;
			continue;
		}

		if (particle)
			_particlesPool.ReleaseData(particle);
	}

	_particles.resize(writeIndex);
}

void ParticleSystem::EmitParticle(float deltaTime)
{
	float deltaSceond = deltaTime / 1000.f;

	_emitTasks.clear();
	_emitSlots.clear();

	// 本帧的粒子预算：上限减掉存活粒子数，由各发射器按顺序领取。
	const std::size_t aliveCount = _particles.size();
	_emitBudget = aliveCount >= static_cast<std::size_t>(m_MaxParticles) ? 0 : static_cast<std::size_t>(m_MaxParticles) - aliveCount;

	std::vector<Entity> entities = m_world->getEntitiesWith<ParticleEmitter, Transform>();
	for (auto& entity : entities)
	{
		auto& trans = entity.getComponent<Transform>();
		auto& emitter = entity.getComponent<ParticleEmitter>();
		if (!emitter.enable)
			continue;

		Emit(trans, emitter, deltaSceond);
	}

	if (_emitTasks.empty())
		return;

	std::for_each(std::execution::par, _emitTasks.begin(), _emitTasks.end(),
		[](const std::function<void()>& task) { task(); });

	_particles.reserve(aliveCount + _emitSlots.size());
	_particles.append_range(std::move(_emitSlots));
	_emitSlots.clear();
}

void ParticleSystem::Emit(const Transform& transform, ParticleEmitter& emitter, float deltaSceond)
{
	auto& params = emitter.emitterParams;
	auto& state = emitter.emitterState;

	if (!params.IsVaild())
		return;

	auto pushEmitTasks = [&](int count)
		{
			if (_emitBudget <= 0)
				return;

			for (int i = 0; i < count && _emitBudget > 0; i++)
			{
				_emitBudget--;

				const std::size_t slot = _emitSlots.size();
				_emitSlots.push_back(nullptr);
				_emitTasks.emplace_back([this, &transform, &params, slot]()
					{
						EmitTaskBody(transform, params, slot);
					});
			}
		};

	state.timeAccumulator += deltaSceond;
	if (state.timeAccumulator < params.warmupTime)
		return;

	auto shouldEmitBurst = [&]()->bool { return params.burstAmount > 0 && (state.burstCountAccumulator <= params.burstCount || params.burstCount == 0); };
	if (shouldEmitBurst())
	{
		state.burstTimeAccumulator += deltaSceond;
		if (state.burstTimeAccumulator > params.burstInterval)
		{
			pushEmitTasks(params.burstAmount);
			state.burstTimeAccumulator -= params.burstInterval;
			state.burstCountAccumulator++;
		}
	}

	float rateScale = 1.0f;
	if (params.emitRateJitter > 0.0f)
	{
		const float jitter = std::clamp(params.emitRateJitter, 0.0f, 1.0f);
		rateScale = std::max(0.0f, 1.0f + Tool::RandomLinear(-jitter, jitter));
	}

	state.emitAmountAccumulator += params.emitRate * rateScale * deltaSceond;
	const uint32_t emitCount = static_cast<uint32_t>(state.emitAmountAccumulator);
	if (emitCount > 0)
	{
		pushEmitTasks(emitCount);
		state.emitAmountAccumulator -= static_cast<float>(emitCount);
	}
}

void ParticleSystem::EmitTaskBody(const Transform& transform, const EmitterParams& params, std::size_t slot)
{
	const glm::mat4 emitterMatrix = transform.GetMatrix();

	glm::vec3 particleInitPosition = params.posGenerator->generate();
	glm::vec3 particleInitDirection = params.dirGenerator->generate(particleInitPosition, transform);
	glm::quat particleInitQuat = params.rotGenerator->generate(particleInitPosition, particleInitDirection, transform);

	Particle* particle = _particlesPool.AllocateData();
	if (!particle)
		return;

	particle->rotation = particleInitQuat;
	particle->scale = params.scaleGenerator->generate();
	particle->velocity = particleInitDirection * params.speedGenerator->generate();
	particle->lifeTime = params.lifeTimeGenerator->generate();
	particle->maxLifeTime = particle->lifeTime;
	particle->position = emitterMatrix * glm::vec4(particleInitPosition + (params.posOffsetGenerator ? params.posOffsetGenerator->generate(particle) : glm::vec3(0)), 1.0f);

	if (params.opacityUpdater) particle->opacityUpdater = params.opacityUpdater->Clone();
	if (params.baseColorUpdater) particle->baseColorUpdater = params.baseColorUpdater->Clone();
	if (params.scaleUpdater) particle->scaleUpdater = params.scaleUpdater->Clone();
	if (params.rotationUpdater) particle->rotationUpdater = params.rotationUpdater->Clone();
	if (params.localAccelerationUpdater) particle->localAccelerationUpdater = params.localAccelerationUpdater->Clone();
	if (params.worldAccelerationUpdater) particle->worldAccelerationUpdater = params.worldAccelerationUpdater->Clone();

	particle->properties = std::make_shared<ParticleProperties>(params.colorGenerator->generate(), params.opacityGenerator->generate());

	particle->properties->position = particle->position;
	particle->properties->rotation = particle->rotation;
	particle->properties->scale = particle->scale;
	particle->properties->transform = MakeParticleTransform(particle->position, particle->rotation, particle->scale);
	particle->properties->blendMode = params.blendMode;
	particle->properties->renderInfo = MakeParticleRenderInfo(params.renderer);

	particle->InitUpdater();

	_emitSlots[slot] = particle;
}

const std::vector<Particle*>& ParticleSystem::GetParticles() const
{
	return _particles;
}

ParticlesPool::ParticlesPool(uint32_t InitResNum, uint32_t maxResNum)
	:ResPool<Particle>(InitResNum, maxResNum)
{}

void ParticlesPool::ResetData(Particle* data)
{
	data->properties.reset();
	data->opacityUpdater.reset();
	data->baseColorUpdater.reset();
	data->scaleUpdater.reset();
	data->rotationUpdater.reset();
	data->localAccelerationUpdater.reset();
	data->worldAccelerationUpdater.reset();
}
