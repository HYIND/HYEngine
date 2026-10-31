#include "Factory/ParticleEmitterFactory.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "PlatformRuntimeComponents.h"

Entity ParticleEmitterFactory::CreateFireEmitter(World& world, const glm::vec3& position, const glm::quat& rotation)
{
	auto entity =  LightFactory::CreatePointLight(world, position, 10.f, Tool::ColorTemperatureToRGB(2200));

	auto& trans = entity.getComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	auto& emitter = entity.addComponent<ParticleEmitter>();
	emitter.emitterParams.emitRate = 150;

	emitter.emitterParams.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(0.15), glm::vec3(0.4));
	emitter.emitterParams.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(230, 180, 39) / 255.f, glm::vec3(255, 220, 220) / 255.f);
	emitter.emitterParams.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(0.5, 1.2);
	emitter.emitterParams.opacityGenerator = std::make_shared<RandomOpacityGenerator>(0.75, 1.0);

	//emitter.emitterParams.posGenerator = std::make_shared<BallGenerator>(0.08f);
	emitter.emitterParams.posGenerator = std::make_shared<PointGenerator>();
	emitter.emitterParams.dirGenerator = std::make_shared<FixedDirectionConeGenerator>(glm::vec3(0, 1, 0), 15);
	emitter.emitterParams.speedGenerator = std::make_shared<RandomSpeedGenerator>(0.5, 0.8);
	emitter.emitterParams.rotGenerator = std::make_shared<FixedRotationGenerator>(Tool::GetQuatFromRotate(90.f, glm::vec3(1, 0, 0)));

	emitter.emitterParams.opacityUpdater = std::make_shared<LinearOpacityUpdater>(0.25f);
	emitter.emitterParams.scaleUpdater = std::make_shared<LinearScaleUpdater>(glm::vec3(0.04));
	emitter.emitterParams.baseColorUpdater = std::make_shared<FireColorUpdater>();
	emitter.emitterParams.worldAccelerationUpdater = std::make_shared<PerlinTurbulenceUpdater>();

	emitter.emitterParams.burstAmount = 0;
	emitter.emitterParams.burstInterval = 2.f;

	ParticleBillboardRenderer renderer;
	renderer.mask = ParticleMask::SoftDisc;
	emitter.emitterParams.renderer = renderer;

	return entity;
}

Entity ParticleEmitterFactory::CreateVortexEmitter(World& world, const glm::vec3& position, const glm::quat& rotation)
{
	Entity emitterEntity = world.createEntity();

	auto& trans = emitterEntity.addComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	auto& emitter = emitterEntity.addComponent<ParticleEmitter>();
	emitter.emitterParams.emitRate = 200;

	emitter.emitterParams.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(1.0), glm::vec3(2.0));
	emitter.emitterParams.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(255, 0, 0) / 255.f, glm::vec3(255, 255, 0) / 255.f);
	emitter.emitterParams.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(12, 20);
	emitter.emitterParams.opacityGenerator = std::make_shared<RandomOpacityGenerator>(0.7, 1.0);

	emitter.emitterParams.posGenerator = std::make_shared<BoxGenerator>(glm::vec3(0.1, 30, 0.1));
	emitter.emitterParams.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.f);
	//emitter.emitterParams.posGenerator = std::make_shared<PointGenerator>();
	//emitter.emitterParams.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(10.f);
	//emitter.emitterParams.posGenerator = std::make_shared<SphericalGenerator>(3.f);
	//emitter.emitterParams.dirGenerator = std::make_shared<RadialDirectionGenerator>(15.f);
	emitter.emitterParams.speedGenerator = std::make_shared<RandomSpeedGenerator>(70, 110);
	emitter.emitterParams.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>();
	emitter.emitterParams.posOffsetGenerator = std::make_shared<RadialPositionOffsetGenerator>();

	emitter.emitterParams.rotationUpdater = std::make_shared<FollowVelocityRotationUpdater>();
	emitter.emitterParams.localAccelerationUpdater = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(60, 0, 0));
	//emitter.emitterParams.worldAccelerationUpdater = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(0, -1, 0));

	emitter.emitterParams.burstAmount = 0;
	emitter.emitterParams.burstInterval = 2.f;

	ParticleBillboardRenderer renderer;
	renderer.mask = ParticleMask::Disc;
	emitter.emitterParams.renderer = renderer;

	return emitterEntity;
}

Entity ParticleEmitterFactory::CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<StaticModel> model)
{
	Entity emitterEntity = world.createEntity();

	auto& trans = emitterEntity.addComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	auto& emitter = emitterEntity.addComponent<ParticleEmitter>();
	emitter.emitterParams.emitRate = 30;

	emitter.emitterParams.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(1.0), glm::vec3(2.0));
	//emitter.emitterParams.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(1.5, 0, 0) , glm::vec3(0.8, 0.8, 0));
	emitter.emitterParams.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(1.5, 1.2, 0.8), glm::vec3(0.9, 0.9, 0.9));
	emitter.emitterParams.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(12, 20);
	emitter.emitterParams.opacityGenerator = std::make_shared<RandomOpacityGenerator>(1.0, 1.0);

	emitter.emitterParams.posGenerator = std::make_shared<BoxGenerator>(glm::vec3(0.1, 30, 0.1));
	emitter.emitterParams.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.f);
	emitter.emitterParams.speedGenerator = std::make_shared<RandomSpeedGenerator>(70, 110);
	emitter.emitterParams.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>();
	emitter.emitterParams.posOffsetGenerator = std::make_shared<RadialPositionOffsetGenerator>();

	emitter.emitterParams.rotationUpdater = std::make_shared<FollowVelocityRotationUpdater>();
	emitter.emitterParams.localAccelerationUpdater = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(60, 0, 0));
	emitter.emitterParams.worldAccelerationUpdater = std::make_shared<GravityAccelerationUpdater>();

	emitter.emitterParams.burstAmount = 0;
	emitter.emitterParams.burstInterval = 2.f;

	ParticleMeshRenderer renderer;
	renderer.meshType = ParticleMeshType::Custom;	// 用用户传进来的模型，而不是内置球
	renderer.model = model;
	emitter.emitterParams.renderer = renderer;

	return emitterEntity;
}

Entity ParticleEmitterFactory::CreateTestEmitter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Material> material)
{
	Entity emitterEntity = world.createEntity();

	auto& trans = emitterEntity.addComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	auto& emitter = emitterEntity.addComponent<ParticleEmitter>();
	emitter.emitterParams.emitRate = 30;

	emitter.emitterParams.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(1.0), glm::vec3(2.0));
	emitter.emitterParams.colorGenerator = std::make_shared<FixedColorGenerator>(glm::vec3(1));
	emitter.emitterParams.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(12, 20);
	emitter.emitterParams.opacityGenerator = std::make_shared<RandomOpacityGenerator>(1.0, 1.0);

	emitter.emitterParams.posGenerator = std::make_shared<BoxGenerator>(glm::vec3(0.1, 30, 0.1));
	emitter.emitterParams.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.f);
	//emitter.emitterParams.posGenerator = std::make_shared<PointGenerator>();
	//emitter.emitterParams.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(10.f);
	//emitter.emitterParams.posGenerator = std::make_shared<SphericalGenerator>(3.f);
	//emitter.emitterParams.dirGenerator = std::make_shared<RadialDirectionGenerator>(15.f);
	emitter.emitterParams.speedGenerator = std::make_shared<RandomSpeedGenerator>(70, 110);
	emitter.emitterParams.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>();
	emitter.emitterParams.posOffsetGenerator = std::make_shared<RadialPositionOffsetGenerator>();

	emitter.emitterParams.rotationUpdater = std::make_shared<FollowVelocityRotationUpdater>();
	emitter.emitterParams.localAccelerationUpdater = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(60, 0, 0));
	//emitter.emitterParams.worldAccelerationUpdater = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(0, -1, 0));

	emitter.emitterParams.burstAmount = 0;
	emitter.emitterParams.burstInterval = 2.f;

	ParticleBillboardRenderer renderer;
	renderer.mask = ParticleMask::None;
	renderer.material = material;
	emitter.emitterParams.renderer = renderer;

	return emitterEntity;
}