#include "Render/Systems/ParticleSystem.h"
#include "CommonComponent.h"
#include "ECSCore/World.h"
#include "Helper/Tools.h"
#include <algorithm>
#include <execution>
#include <iterator>

namespace
{
	// 粒子数超过这个值才并行（小发射器串行更快，省掉线程调度）
	constexpr std::size_t kParallelThreshold = 200;

	template<typename It, typename F>
	void ForEachMaybeParallel(It first, It last, F&& f)
	{
		if (static_cast<std::size_t>(std::distance(first, last)) >= kParallelThreshold)
			std::for_each(std::execution::par_unseq, first, last, f);
		else
			std::for_each(first, last, f);
	}

	inline void AdvanceParticle(ParticleState& particle, float deltaTime, float deltaSecond)
	{
		// 取裸指针调用：不拷 shared_ptr，省掉每个粒子每帧 6 次原子引用计数
		glm::vec3 localAccl(0.f);
		glm::vec3 worldAccl(0.f);
		if (auto* updater = particle.opacityUpdater.get()) particle.opacity = updater->update(particle, deltaTime);
		if (auto* updater = particle.baseColorUpdater.get()) particle.baseColor = updater->update(particle, deltaTime);
		if (auto* updater = particle.scaleUpdater.get()) particle.scale = updater->update(particle, deltaTime);
		if (auto* updater = particle.rotationUpdater.get()) particle.rotation = updater->update(particle, deltaTime);
		if (auto* updater = particle.localAccelerationUpdater.get()) localAccl = updater->update(particle, deltaTime);
		if (auto* updater = particle.worldAccelerationUpdater.get()) worldAccl = updater->update(particle, deltaTime);

		glm::vec3 accl = worldAccl;
		if (localAccl != glm::vec3(0.f))
			accl += particle.rotation * localAccl;

		particle.velocity += accl * deltaSecond;
		particle.position += particle.velocity * deltaSecond;
	}
}

ParticleSystem::ParticleSystem()
{}

void ParticleSystem::update(float deltaTime)
{
	UpdateParticle(deltaTime);
	EmitParticle(deltaTime);
	BuildFrameData();

	// 逻辑性能面板的规模数据（耗时由 SystemManager 统一记）
	if (auto* profile = m_world->GetCurrentLogicProfile())
	{
		profile->SetCounter("存活粒子", static_cast<int64_t>(_particleCount));
		profile->SetCounter("粒子发射器", static_cast<int64_t>(_emitters.size()));
	}
}

void ParticleSystem::UpdateParticle(float deltaTime)
{
	const float deltaSecond = deltaTime / 1000.f;

	_particleCount = 0;
	for (auto& [id, emitter] : _emitters)
	{
		auto& particles = emitter.particles;
		ForEachMaybeParallel(particles.begin(), particles.end(),
			[deltaTime, deltaSecond](ParticleState& particle)
			{
				particle.lifeTime -= deltaSecond;
				if (particle.lifeTime <= 0.f)
					return;

				AdvanceParticle(particle, deltaTime, deltaSecond);
			});

		// 就地压缩：erase_if 保持存活粒子的相对顺序（批次按顺序切分，顺序不能乱）
		std::erase_if(particles, [](const ParticleState& particle) { return particle.lifeTime <= 0.f; });
		_particleCount += particles.size();
	}
}

void ParticleSystem::EmitParticle(float deltaTime)
{
	const float deltaSecond = deltaTime / 1000.f;

	for (auto& [id, emitter] : _emitters)
		emitter.seen = false;

	std::vector<Entity> entities = m_world->getEntitiesWith<ParticleEmitter, Transform>();
	for (auto& entity : entities)
	{
		auto& trans = entity.getComponent<Transform>();
		auto& emitter = entity.getComponent<ParticleEmitter>();

		auto& container = _emitters[entity.getId()];
		container.seen = true;
		container.visible = emitter.visible;

		if (!emitter.enable)
			continue;

		// 预算按发射器各自独立：上限减掉这个发射器当前存活的粒子数，发射器之间互不挤占
		auto& particles = container.particles;
		const std::size_t maxParticles = static_cast<std::size_t>(std::max(emitter.emitterParams.maxParticles, 0));
		const std::size_t budget = particles.size() >= maxParticles ? 0 : maxParticles - particles.size();

		// 预算为 0 时也要调用：发射累加器、突发计数照常推进，被截掉的部分丢弃（和改之前一致）
		const std::size_t count = ComputeEmitCount(emitter, deltaSecond, budget);
		if (count == 0)
			continue;

		const std::size_t start = particles.size();
		particles.resize(start + count);

		const glm::mat4 emitterMatrix = trans.GetMatrix();
		const EmitterParams& params = emitter.emitterParams;
		ForEachMaybeParallel(particles.begin() + start, particles.end(),
			[&trans, &emitterMatrix, &params](ParticleState& particle)
			{
				InitParticle(particle, trans, emitterMatrix, params);
			});

		_particleCount += count;
	}

	// 发射器已经删掉、粒子也死光了的，回收容器
	std::erase_if(_emitters, [](const auto& entry) { return !entry.second.seen && entry.second.particles.empty(); });
}

std::size_t ParticleSystem::ComputeEmitCount(ParticleEmitter& emitter, float deltaSceond, std::size_t budget)
{
	auto& params = emitter.emitterParams;
	auto& state = emitter.emitterState;

	if (!params.IsVaild())
		return 0;

	std::size_t count = 0;
	auto request = [&](int amount)
		{
			if (amount <= 0 || budget == 0)
				return;
			const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(amount), budget);
			budget -= n;
			count += n;
		};

	state.timeAccumulator += deltaSceond;
	if (state.timeAccumulator < params.warmupTime)
		return 0;

	auto shouldEmitBurst = [&]()->bool { return params.burstAmount > 0 && (state.burstCountAccumulator <= params.burstCount || params.burstCount == 0); };
	if (shouldEmitBurst())
	{
		state.burstTimeAccumulator += deltaSceond;
		if (state.burstTimeAccumulator > params.burstInterval)
		{
			request(params.burstAmount);
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
		request(static_cast<int>(emitCount));
		state.emitAmountAccumulator -= static_cast<float>(emitCount);
	}

	return count;
}

void ParticleSystem::InitParticle(ParticleState& particle, const Transform& transform, const glm::mat4& emitterMatrix, const EmitterParams& params)
{
	glm::vec3 particleInitPosition = params.posGenerator->generate();
	glm::vec3 particleInitDirection = params.dirGenerator->generate(particleInitPosition, transform);
	glm::quat particleInitQuat = params.rotGenerator->generate(particleInitPosition, particleInitDirection, transform);

	particle.rotation = particleInitQuat;
	particle.scale = params.scaleGenerator->generate();
	particle.velocity = particleInitDirection * params.speedGenerator->generate();
	particle.lifeTime = params.lifeTimeGenerator->generate();
	particle.maxLifeTime = particle.lifeTime;
	// 位置偏移生成器会读速度（RadialPositionOffsetGenerator），所以放在速度之后
	particle.position = emitterMatrix * glm::vec4(particleInitPosition + (params.posOffsetGenerator ? params.posOffsetGenerator->generate(particle) : glm::vec3(0)), 1.0f);

	if (params.opacityUpdater) particle.opacityUpdater = params.opacityUpdater->Clone();
	if (params.baseColorUpdater) particle.baseColorUpdater = params.baseColorUpdater->Clone();
	if (params.scaleUpdater) particle.scaleUpdater = params.scaleUpdater->Clone();
	if (params.rotationUpdater) particle.rotationUpdater = params.rotationUpdater->Clone();
	if (params.localAccelerationUpdater) particle.localAccelerationUpdater = params.localAccelerationUpdater->Clone();
	if (params.worldAccelerationUpdater) particle.worldAccelerationUpdater = params.worldAccelerationUpdater->Clone();

	particle.baseColor = params.colorGenerator->generate();
	particle.opacity = std::clamp(params.opacityGenerator->generate(), 0.f, 1.f);
	particle.blendMode = params.blendMode;
	particle.renderInfo = MakeParticleRenderInfo(params.renderer);

	// Init 会读初始颜色 / 不透明度 / 缩放，必须在它们都设好之后
	particle.InitUpdater();
}

void ParticleSystem::BuildFrameData()
{
	struct BuildJob
	{
		const EmitterParticles* emitter = nullptr;
		uint32_t offset = 0;
		std::vector<ParticleRenderBatch> batches;
	};

	std::vector<BuildJob> jobs;
	jobs.reserve(_emitters.size());
	uint32_t total = 0;
	for (auto& [id, emitter] : _emitters)
	{
		if (!emitter.visible || emitter.particles.empty())
			continue;
		jobs.push_back(BuildJob{ .emitter = &emitter, .offset = total });
		total += static_cast<uint32_t>(emitter.particles.size());
	}

	if (total == 0)
	{
		_frameData = nullptr;
		return;
	}

	auto frame = std::make_shared<ParticleFrameData>();
	frame->particles.resize(total);

	// 每个发射器各写自己那段，互不重叠，可以并行
	std::for_each(std::execution::par, jobs.begin(), jobs.end(),
		[&frame](BuildJob& job)
		{
			const auto& particles = job.emitter->particles;
			ParticleRenderBatch* current = nullptr;
			for (std::size_t i = 0; i < particles.size(); ++i)
			{
				const ParticleState& particle = particles[i];

				// 按渲染参数游程切分：平时一个发射器一个批次，改了网格 / 材质 / 混合模式后新旧粒子各一段
				if (!current || current->blendMode != particle.blendMode || !current->renderInfo.SameBatch(particle.renderInfo))
				{
					ParticleRenderBatch batch;
					batch.renderInfo = particle.renderInfo;
					batch.blendMode = particle.blendMode;
					batch.firstParticle = job.offset + static_cast<uint32_t>(i);
					job.batches.push_back(std::move(batch));
					current = &job.batches.back();
				}

				ParticleRenderParticle& out = frame->particles[job.offset + i];
				out.position = particle.position;
				out.opacity = particle.opacity;
				out.rotation = particle.rotation;
				out.scale = particle.scale;
				out.color = particle.baseColor;

				const glm::vec3 absScale = glm::abs(particle.scale);
				const float extent = particle.renderInfo.mesh
					? std::max(absScale.x, std::max(absScale.y, absScale.z))
					: std::max(absScale.x, absScale.y);

				current->centerMin = glm::min(current->centerMin, particle.position);
				current->centerMax = glm::max(current->centerMax, particle.position);
				current->maxExtent = std::max(current->maxExtent, extent);
				current->particleCount++;
			}
		});

	std::size_t batchCount = 0;
	for (auto& job : jobs)
		batchCount += job.batches.size();
	frame->batches.reserve(batchCount);
	for (auto& job : jobs)
		std::move(job.batches.begin(), job.batches.end(), std::back_inserter(frame->batches));

	_frameData = std::move(frame);
}

std::shared_ptr<const ParticleFrameData> ParticleSystem::GetFrameData() const
{
	return _frameData;
}

std::size_t ParticleSystem::GetParticleCount() const
{
	return _particleCount;
}
