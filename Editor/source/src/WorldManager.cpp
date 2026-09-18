#include "WorldManager.h"
#include "CommonSystems.h"
#include "GamePlaySystems.h"
#include "GameRuntimeSystems.h"
#include "ECSCore\World.h"
#include "Helper/DynamicFpsController.h"

#include "Factory/LightFactory.h"
#include "Factory/ParticleEmitterFactory.h"
#include "Factory/CharacterFactory.h"
#include "VulkanRenderEngine/General/RenderHelp.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "GameRuntimeComponents.h"

#include "GeneralManager/FocusManager.h"
#include "GeneralManager/MouseManager.h"
#include "GeneralManager/KeyMapManaer.h"

void AddPickProxy(Entity& entity)
{
	if (!entity.hasComponent<Transform>())
		return;

	if (!entity.hasComponent<Physics>())
	{
		auto& physics = entity.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Kinematic;
		physics.isSensor = true;
		physics.allowSleep = false;
		physics.collisionShape.AddBoxShape(glm::vec3(0.5));

		if (auto renderModel = entity.tryGetComponent<RenderModel>(); renderModel && renderModel->model)
		{
			auto aabb = renderModel->model->GetAABB();
			auto halfExtend = (aabb.max - aabb.min) / 2.f;
			if (halfExtend.x > 0.5 || halfExtend.y > 0.5 || halfExtend.z > 0.5)
				physics.collisionShape.AddBoxShape(halfExtend);
		}
	}
	else
	{
		auto& physics = entity.getComponent<Physics>();
		physics.allowSleep = false;
	}
	entity.addComponent<EditorPick>();
}

void AddVariableMaterial(Entity& entity)
{
	if (!entity.hasComponent<RenderModel>())
		return;

	if (entity.hasComponent<VariableMaterial>())
		return;

	auto& renderModel = entity.getComponent<RenderModel>();
	if (!renderModel.model)
		return;

	auto model = renderModel.model;
	auto& info = model->getMeshInfos();
	if (info.empty())
		return;

	auto& material = info[0].material;
	if (!material)
		return;

	auto& variableMaterial = entity.addComponent<VariableMaterial>();
	variableMaterial.data.albedo = material->GetAlbedo();
	variableMaterial.data.metallic = material->GetMetallic();
	variableMaterial.data.roughness = material->GetRoughness();
	variableMaterial.data.opacity = material->GetOpacity();
	variableMaterial.data.alphamode = (VariableMaterialData::AlphaMode)material->GetAlphaMode();
	variableMaterial.data.twosided = material->GetTwoSided();
	variableMaterial.data.emissionColor = material->GetEmissionColor();
	variableMaterial.data.emissionStrength = material->GetEmissionStrength();
}

void SetNameTag(Entity entity, const std::string& name)
{
	if (!entity)
		return;

	if (!entity.hasComponent<NameTag>())
	{
		auto& nameTag = entity.addComponent<NameTag>();
		nameTag.name = name;
	}
	else
	{
		auto& nameTag = entity.getComponent<NameTag>();
		nameTag.name = name;
	}
}

void LoadInitScene(World& world)
{
	{
		Entity entity = LightFactory::CreateDirLight(world, glm::vec3(1, -1, 1), glm::vec3(1.0f), 3.5, true, 4, 3000, 3000);
		AddPickProxy(entity);
	}

	{
		std::array<std::string, 6> faces
		{
			"Test/skybox/box2/right.png",
			"Test/skybox/box2/left.png",
			"Test/skybox/box2/top.png",
			"Test/skybox/box2/bottom.png",
			"Test/skybox/box2/front.png",
			"Test/skybox/box2/back.png"
		};
		auto skyboxcube = std::make_shared<TextureCube>(faces);
		Entity entity = world.createEntityWithTag<TagSkyBox>();
		entity.addComponent<SkyBox>(skyboxcube);

		SetNameTag(entity, "skybox");
	}

	{

		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = -90.f;
		controller.pitch = -45.f;
		freeEntity.addComponent<TagCurrentControl>();
		auto& tag = freeEntity.getComponent<TagFreeCamera>();
		tag.velocity = 0.25;

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>(glm::vec3(0, 10, 10));
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = freeEntity;
			camerafollow.offset = glm::vec3(0, 0, 0);
			world.SetMainCamera(cameraEntity);

			//if (freeEntity.hasComponent<TagCurrentControl>() && cameraEntity.hasComponent<TagMainCamera>())
			//{
			//	auto light = LightFactory::CreateSpotLight(world, glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 100.f, 17.5, 30);
			//	light.addComponent<TagLightShowLight>();
			//	auto& follow = light.addComponent<LightFollow>();
			//	follow.target = cameraEntity;
			//}
			SetNameTag(cameraEntity, "freeCameraEntity");
		}
		SetNameTag(freeEntity, "freeEntity");
	}

	{
		Entity cube = world.createEntity();
		auto& trans = cube.addComponent<Transform>();
		trans.position = { 0,3,0 };

		auto& physics = cube.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.mass = 5.f;
		physics.collisionShape.AddBoxShape();

		auto& rendermodel = cube.addComponent<RenderModel>();
		rendermodel.model = GetCubeModel(glm::vec3(0.5f), 1.0f);

		AddPickProxy(cube);
		AddVariableMaterial(cube);
		SetNameTag(cube, "cube_1");
	}

	{
		float radius = 3.0;

		Entity sphere = world.createEntity();
		auto& trans = sphere.addComponent<Transform>();
		trans.position = { 3,3,0 };
		trans.scale = glm::vec3(radius / 0.5f);

		auto& physics = sphere.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.mass = 5.f;
		physics.collisionShape.AddSphereShape(0.5f);

		auto model = GetSphereModel(0.5, 72, 36);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model;

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			material->SetMetallic(0.5);
			material->SetRoughness(0.3f);
			material->SetAlbedo(glm::vec3(1.0f));
		}

		AddPickProxy(sphere);
		AddVariableMaterial(sphere);
		SetNameTag(sphere, "sphere_1");
	}


	{
		float floorSize = 50.f;
		auto model = GetCubeModel(glm::vec3(0.5f), 1.0f);

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 1, 1));

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, 0, 0);
		trans.scale = glm::vec3(floorSize, 1.f, floorSize);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape();

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;

		//{
		//	if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		//	{
		//		auto& material = model->getMeshInfos()[0].material;
		//		material->SetAlbedo(glm::vec3(0.85, 0.87, 0.89));
		//		material->SetMetallic(0.93);
		//		material->SetRoughness(0.05f);
		//	}
		//}

		AddPickProxy(floor);
		AddVariableMaterial(floor);
		SetNameTag(floor, "floor");
	}
}

WorldManager* WorldManager::Instance() {
	static WorldManager* instance = new WorldManager();
	return instance;
}

WorldManager::WorldManager()
{
	_triBuffer = std::make_shared<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>>();
	for (int i = 0; i < 3; i++)
		_triBuffer->setInitialValue(i, std::make_shared<Render::RenderFrameData>());
}

WorldManager::~WorldManager() {}

void WorldManager::SetRender(std::shared_ptr<VulkanRenderer> renderer)
{
	_renderer = renderer;
	pendingWidth = _renderer->GetWidth();
	pendingHeight = _renderer->GetHeight();
}

void WorldManager::InitWorld()
{
	auto world = std::make_shared<World>();

	// 通过World注册系统
	auto& inputSystem = world->registerSystem<LocalInputSystem>();
	auto& movementSystem = world->registerSystem<MovementSystem>();
	auto& renderSystem = world->registerSystem<RenderSystem>();
	auto& lifetimeSystem = world->registerSystem<LifetimeSystem>();
	auto& physicsSystem = world->registerSystem<PhysicsSystem>();
	auto& destroySystem = world->registerSystem<DestroySystem>();
	auto& healthSystem = world->registerSystem<HealthSystem>();
	auto& audioSystem = world->registerSystem<AudioSystem>();
	auto& cameraFollowSystem = world->registerSystem<CameraFollowSystem>();
	auto& weaponSystem = world->registerSystem<WeaponSystem>();
	auto& animationSystem = world->registerSystem<AnimationSystem>();
	auto& lightShowSystem = world->registerSystem<LightShowSystem>();
	auto& lightFollowSystem = world->registerSystem<LightFollowSystem>();
	auto& particleSystem = world->registerSystem<ParticleSystem>();
	auto& laserBeamSystem = world->registerSystem<LaserBeamSystem>();


	lifetimeSystem.setPriority(10000);
	inputSystem.setPriority(1000);
	movementSystem.setPriority(500);
	weaponSystem.setPriority(400);
	physicsSystem.setPriority(300);
	laserBeamSystem.setPriority(250);
	particleSystem.setPriority(200);
	cameraFollowSystem.setPriority(-3000);
	lightFollowSystem.setPriority(-3100);
	destroySystem.setPriority(-8000);
	animationSystem.setPriority(-9000);
	renderSystem.setPriority(-10000);

	renderSystem.SetTriBuffer(_triBuffer);
	renderSystem.SetOpenGLRender(_renderer);

	LoadInitScene(*world);

	_world = world;
	_world->update(16);
}

void WorldManager::RunWorld()
{
	if (!_world || _world->isRunning())
		return;

	// 启动世界
	_world->setLogicDeltaTime(1000.f / 165.f);
	_world->setFixedDeltaTime(1000.f / 60.f);
	_world->start();
	_worldStop = false;
	_worldThread = std::make_shared<std::thread>(&WorldManager::WorldLoop, this);
}

void WorldManager::RunPushFrame()
{
	if (!_framePushStop)
		return;

	_framePushStop = false;
	_framePushThread = std::make_shared<std::thread>(&WorldManager::PushFrameLoop, this);
}

void WorldManager::StopPushFrame()
{
	_framePushStop = true;
	if (_framePushThread)
	{
		if (_framePushThread->joinable())
			_framePushThread->join();
		_framePushThread.reset();
	}
}

void WorldManager::WaitImage(const std::function<void(std::shared_ptr<Texture2D>)>& callback) {
	_renderer->WaitImage([&](std::shared_ptr<Texture2D> tex, std::shared_ptr<RenderState> state) {
		float timeDiff = (state->renderRecord.frameEndMicroTimeStamp - state->renderRecord.frameStartMicroTimeStamp) / 1000.f;
		estimate.run(timeDiff / _renderer->GetMaxFramesInFlight());
		controller.setCurTargetFps(estimate.getCurTargetFps());
		callback(tex);
		});
}

void WorldManager::PushFrameLoop()
{
	controller.reset();

	while (!_framePushStop)
	{
		auto framedata = _triBuffer->acquireReadBuffer();
		if (!framedata)
			return;

		if (auto r = _renderer)
		{
			auto state = RenderStateBuilder()
				.SetCamera(framedata->projection, framedata->view,
					framedata->position, framedata->direction, framedata->directionUp, framedata->directionRight,
					framedata->nearPlane, framedata->farPlane, framedata->fov)
				.Build();

			Render::RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(framedata, *state);

			r->PushFrameState(state);
		}

		controller.run();
	}
}

void WorldManager::WorldLoop()
{
	int targetfps = (1000.f / std::min(_world->getFixedDeltaTime(), _world->getLogicDeltaTime())) + 1;
	DynamicFpsController fpscontroller(targetfps);
	fpscontroller.reset();

	// 主循环
	while (!_worldStop)
	{
		float dt = fpscontroller.getTimeDiffMS();

		_world->update(dt);

		fpscontroller.run();
	}
}

void WorldManager::PauseWorld()
{
	auto world = _world;
	if (!world)
		return;

	for (auto& name : _world->getSystemNames())
	{
		//std::cout << std::format("name = {}\n", name);
		bool isRender = name.find("Render") != std::string::npos;
		bool isCmaera = name.find("Camera") != std::string::npos;
		bool isInput = name.find("Input") != std::string::npos;
		bool shouldDisable = !isRender && !isCmaera && !isInput;
		if (shouldDisable)
			_world->setSystemEnabled(name, false);
	}
}

void WorldManager::ContinueWorld()
{
	auto world = _world;
	if (!world)
		return;

	for (auto& name : _world->getSystemNames())
	{
		_world->setSystemEnabled(name, true);
	}
}

void WorldManager::StopWorld()
{
	_worldStop = true;
	if (_worldThread)
	{
		if (_worldThread->joinable())
			_worldThread->join();
		_worldThread.reset();
	}
	_world->stop();
	_world.reset();
}

Entity WorldManager::PickObject(const glm::vec3& origin, const glm::vec3& direction)
{
	auto view = _triBuffer->acquireReadBuffer()->view;
	auto projection = _triBuffer->acquireReadBuffer()->projection;

	if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
	{
		auto result = physicsSystem->raycast(origin, origin + direction * (2000.f));
		if (!result.hit)
			return Entity();

		if (result.hitEntity.hasComponents<Transform, EditorPick>())
			return result.hitEntity;
	}

	return Entity();
}

RaycastHit WorldManager::RayCast(const glm::vec3& origin, const glm::vec3& direction)
{
	auto view = _triBuffer->acquireReadBuffer()->view;
	auto projection = _triBuffer->acquireReadBuffer()->projection;

	if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
		return physicsSystem->raycast(origin, origin + direction * (2000.f));

	return RaycastHit();
}

void WorldManager::SetInputActive(bool enable)
{
	FocusManager::Instance()->SetFocus(enable);
	FocusManager::Instance()->SetActive(enable);

	MouseManager::Instance()->ReSet(100, 100);
}

void WorldManager::RotateCamera(float deltaX, float deltaY)
{
	if (!_world->HasMainCameraEntity())
		return;
	auto entity = _world->GetMainCameraEntity();
	if (!entity)
		return;

	MouseManager::Instance()->ReSet(100, 100);
	MouseManager::Instance()->InputPos(100 + deltaX, 100 + deltaY);
}

void WorldManager::PanCamera(float deltaX, float deltaY)
{
	if (!_world->HasMainCameraEntity())
		return;
	auto entity = _world->GetMainCameraEntity();
	if (!entity)
		return;

}

void WorldManager::ZoomCamera(float delta)
{
	if (!_world->HasMainCameraEntity())
		return;
	auto entity = _world->GetMainCameraEntity();
	if (!entity)
		return;

}

RenderOption WorldManager::GetOption() const
{
	return _renderer->GetOption();
}

void WorldManager::SetOption(RenderOption option)
{
	_renderer->SetOption(option);
}

bool WorldManager::ResizeVulkan(uint32_t width, uint32_t height)
{
	if (pendingWidth != width || pendingHeight != height)
	{
		pendingWidth = width;
		pendingHeight = height;
		resizePending = true;
		resizeTimeStamp = Tool::GetTimestampMilliseconds();
	}
	else
	{
		if (resizePending && Tool::GetTimestampMilliseconds() - resizeTimeStamp > 200 && _renderer)
		{
			_renderer->Resize(pendingWidth, pendingHeight);
			resizePending = false;
			return true;
		}
	}

	return false;
}

Entity WorldManager::CreateModelEntity(std::shared_ptr<Model> model)
{
	if (!model)
		return Entity();

	Entity entity;
	_world->SubmitCommand([&] {
		entity = _world->createEntity();
		auto& trans = entity.addComponent<Transform>();
		trans.position = glm::vec3(0, 0, 0);
		trans.rotation = glm::identity<glm::quat>();

		auto& rendermodel = entity.addComponent<RenderModel>();
		rendermodel.model = model;

		AddPickProxy(entity);
		AddVariableMaterial(entity);
		SetNameTag(entity, std::format("model_entity_{}", entity.getId()));
		}).get();

	return entity;
}

bool WorldManager::DuplicateEntity(Entity oriEntity, Entity& newEntity)
{
	auto world = _world;
	if (!_world || !_world->isRunning())
		return false;

	bool isSuccess = false;
	world->SubmitCommand([&isSuccess, &newEntity, entity = oriEntity, world = world]() -> void {
		if (!entity || !world)
		{
			isSuccess = false;
			return;
		}

		newEntity = world->DuplicateEntity(entity);
		if (newEntity.hasComponent<NameTag>())
		{
			auto& tag = newEntity.getComponent<NameTag>();
			tag.name += "_copy";
		}
		if (auto renderModel = newEntity.tryGetComponent<RenderModel>(); renderModel && renderModel->model)
			renderModel->model = renderModel->model->Clone(true, true, true);

		isSuccess = true;
		}
	).get();

	return isSuccess && newEntity;
}

std::shared_ptr<VulkanRenderer> WorldManager::GetVulkanRener()
{
	return _renderer;
}

std::shared_ptr<World> WorldManager::GetWorld()
{
	return _world;
}

std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> WorldManager::GetTriBuffer()
{
	return _triBuffer;
}
