#include "ECS/Systems/ParticleSystem.h"
#include "ECS/Core/World.h"

ParticleSystem::ParticleSystem()
	:m_MaxParticles(20000), _particlesPool(3000, 10000)
{
}

void ParticleSystem::update(float deltaTime)
{
	//auto start = Tool::GetTimestampMilliseconds();
	UpdateParticle(deltaTime);
	EmitParticle(deltaTime);
	//std::cout << std::format("ParticleSystem upadte cost {}ms, Particle number = {}\n", Tool::GetTimestampMilliseconds() - start, _particles.size());
}

void ParticleSystem::UpdateParticle(float deltaTime)
{
	static auto GetTransform = [](const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)-> glm::mat4 {
		return glm::translate(glm::mat4(1.0f), position)
			* glm::toMat4(rotation)
			* glm::scale(glm::mat4(1.0f), scale);
		};

	float deltaSceond = deltaTime / 1000.f;

	for (auto& particle : _particles)
	{
		particle->lifeTime -= deltaSceond;
		if (particle->lifeTime <= 0.f)
			continue;
		if (!particle->properties)
			continue;


		glm::vec localaccl = glm::vec3(0.f);
		glm::vec worldaccl = glm::vec3(0.f);
		if (particle->opacityUpdater) particle->properties->opacity = particle->opacityUpdater->update(particle, deltaTime);
		if (particle->baseColorUpdater) particle->properties->baseColor = particle->baseColorUpdater->update(particle, deltaTime);
		if (particle->scaleUpdater) particle->scale = particle->scaleUpdater->update(particle, deltaTime);
		if (particle->rotationUpdater) particle->rotation = particle->rotationUpdater->update(particle, deltaTime);
		if (particle->localAccelerationUpdater) localaccl = particle->localAccelerationUpdater->update(particle, deltaTime);
		if (particle->worldAccelerationUpdater) worldaccl = particle->worldAccelerationUpdater->update(particle, deltaTime);

		glm::vec3 accl = worldaccl;
		if (glm::length2(localaccl) != 0)
			accl += particle->rotation * localaccl;

		particle->velocity += accl * deltaSceond;
		particle->position += particle->velocity * deltaSceond;

		particle->properties->position = particle->position;
		particle->properties->rotation = particle->rotation;
		particle->properties->scale = particle->scale;
		particle->properties->transform = GetTransform(particle->position, particle->rotation, particle->scale);
	}

	auto itDead = std::partition(
		_particles.begin(),
		_particles.end(),
		[](const Particle* p) {
			return p && p->lifeTime > 0.0f;  // 活着放在前面
		}
	);

	for (auto it = itDead; it != _particles.end(); ++it) {
		Particle* particle = *it;
		if (particle)
			_particlesPool.ReleaseData(particle);
	}

	_particles.erase(itDead, _particles.end());
}

void ParticleSystem::EmitParticle(float deltaTime)
{
	float deltaSceond = deltaTime / 1000.f;

	std::vector<Entity> entities = m_world->getEntitiesWith<ParticleEmitter, Transform>();
	for (auto& entity : entities)
	{
		auto& trans = entity.getComponent<Transform>();
		auto& emitter = entity.getComponent<ParticleEmitter>();
		if (!emitter.enable)
			continue;

		Emit(trans, emitter, deltaSceond);
	}
}

void ParticleSystem::Emit(const Transform& transform, ParticleEmitter& emitter, float deltaSceond)
{
	if (_particles.size() >= m_MaxParticles)
		return;

	auto& params = emitter.emitterParams;
	auto& state = emitter.emitterState;

	static auto GetTransform = [](const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)-> glm::mat4 {
		return glm::translate(glm::mat4(1.0f), position)
			* glm::toMat4(rotation)
			* glm::scale(glm::mat4(1.0f), scale);
		};

	auto emitParticle = [&](int count)-> void
		{
			for (int i = 0; i < count; i++)
			{
				if (_particles.size() >= m_MaxParticles)
					break;

				if (!params.IsVaild())
					continue;

				glm::vec3 particleInitPosition = params.posGenerator->generate();
				glm::vec3 particleInitDirection = params.dirGenerator->generate(particleInitPosition, transform);
				glm::quat particleInitQuat = params.rotGenerator->generate(particleInitPosition, particleInitDirection, transform);

				Particle* particle = _particlesPool.AllocateData();
				particle->rotation = particleInitQuat;
				particle->scale = params.scaleGenerator->generate();
				particle->velocity = particleInitDirection * params.speedGenerator->generate();
				particle->lifeTime = params.lifeTimeGenerator->generate();
				particle->maxLifeTime = particle->lifeTime;
				particle->position = transform.getMatrix() * glm::vec4(particleInitPosition + (params.posOffsetGenerator ? params.posOffsetGenerator->generate(particle) : glm::vec3(0)), 1.0f);

				if (params.opacityUpdater) particle->opacityUpdater = params.opacityUpdater->Clone();
				if (params.baseColorUpdater) particle->baseColorUpdater = params.baseColorUpdater->Clone();
				if (params.scaleUpdater) particle->scaleUpdater = params.scaleUpdater->Clone();
				if (params.rotationUpdater) particle->rotationUpdater = params.rotationUpdater->Clone();
				if (params.localAccelerationUpdater) particle->localAccelerationUpdater = params.localAccelerationUpdater->Clone();
				if (params.worldAccelerationUpdater) particle->worldAccelerationUpdater = params.worldAccelerationUpdater->Clone();
				if (params.type == ParticleType::Color)
				{
					glm::vec3 baseColor;
					auto properties = std::make_shared<ColorParticleProperties>(params.colorGenerator->generate(), params.opacityGenerator->generate(), params.shape);
					particle->properties = properties;
				}
				else if (params.type == ParticleType::Color_Texture)
				{
					auto properties = std::make_shared<TextureParticleProperties>(params.texture, params.colorGenerator->generate(), params.opacityGenerator->generate(), params.shape);
					particle->properties = properties;
				}
				else if (params.type == ParticleType::Model)
				{
					auto properties = std::make_shared<ModelParticleProperties>(params.model, params.colorGenerator->generate(), params.opacityGenerator->generate());
					particle->properties = properties;
				}

				if (!particle->properties)
				{
					_particlesPool.ReleaseData(particle);
					return;
				}

				particle->properties->position = particle->position;
				particle->properties->rotation = particle->rotation;
				particle->properties->scale = particle->scale;
				particle->properties->transform = GetTransform(particle->position, particle->rotation, particle->scale);
				particle->properties->particleType = params.type;

				particle->InitUpdater();

				_particles.push_back(particle);
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
			emitParticle(params.burstAmount);
			state.burstTimeAccumulator -= params.burstInterval;
			state.burstCountAccumulator++;
		}
	}

	state.emitAmountAccumulator += params.emitRate * deltaSceond;
	while (state.emitAmountAccumulator > 1.f)
	{
		emitParticle(1);
		state.emitAmountAccumulator--;
	}
}

const std::vector<Particle*>& ParticleSystem::GetParticles() const
{
	return _particles;
}

ParticlesPool::ParticlesPool(uint32_t InitResNum, uint32_t maxResNum)
	:ResPool<Particle>(InitResNum, maxResNum)
{
}

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
