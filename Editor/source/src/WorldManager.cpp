#include "WorldManager.h"
#include "CommonSystems.h"
#include "GamePlaySystems.h"
#include "GameRuntimeSystems.h"
#include "ECSCore\World.h"
#include "Helper/DynamicFpsController.h"

#include "Factory/LightFactory.h"
#include "Factory/ParticleEmitterFactory.h"
#include "Factory/CharacterFactory.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
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
		auto skyboxcube = std::make_shared<TextureCube>(faces, true);
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
		auto model = GetFloorModel();
		model->MakeScale(glm::vec3(floorSize));

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 1, 1));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, 0, 0);
		trans.rotation = glm::identity<glm::quat>();

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(Half_SizeX, 0.2f, Half_SizeZ), glm::vec3(0, -0.2, 0));

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

WorldManager::WorldManager() {
	_openglRenderer = std::make_shared<OpenGLRenderer>();
	_triBuffer = std::make_shared<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>>();
	for (int i = 0; i < 3; i++)
		_triBuffer->setInitialValue(i, std::make_shared<Render::RenderFrameData>());
}

WorldManager::~WorldManager() {}

void WorldManager::InitOpenGLRender(uint32_t width, uint32_t height)
{
	_openglRenderer->Init(width, height);
	pendingWidth = _openglRenderer->GetWidth();
	pendingHeight = _openglRenderer->GetHeight();
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
	renderSystem.SetOpenGLRender(_openglRenderer);

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
	_stop = false;
	_worldThread = std::make_shared<std::thread>(&WorldManager::WorldLoop, this);
}

void WorldManager::RenderFrame()
{
	auto framedata = _triBuffer->acquireReadBuffer();
	if (!framedata)
		return;

	if (auto r = _openglRenderer)
	{
		RenderState state = RenderStateBuilder()
			.SetCamera(framedata->projection, framedata->view,
				framedata->position, framedata->direction, framedata->directionUp, framedata->directionRight,
				framedata->nearPlane, framedata->farPlane, framedata->fov)
			.Build();

		AnalysisRenderFrameData(framedata, state);

		r->EarlyProcess(state);
		r->Draw(state);
	}
}

void WorldManager::AnalysisRenderFrameData(std::shared_ptr<Render::RenderFrameData>& framedata, RenderState& state)
{
	auto& contexts = framedata->GL_Contexts;

	auto& dirLightInfos = state.lights.dirLightInfos;
	auto& pointLightInfos = state.lights.pointLightInfos;;
	auto& spotLightInfos = state.lights.spotLightInfos;

	for (auto& context : contexts)
	{
		if (!context || !context->data)
			continue;

		switch (context->type)
		{
		case OpenGLRenderContext::RenderContextType::Model:
		{
			processSceneModel(
				state,
				state.objects.sceneRenderData,
				std::static_pointer_cast<OpenGLRenderContext::SceneModelRenderData>(context->data)
			);
			break;
		}
		case OpenGLRenderContext::RenderContextType::DirLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::DirLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<DirLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				dirLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::PointLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::PointLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<PointLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				pointLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::SpotLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::SpotLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<SpotLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				spotLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::FirstPersonModel:
		{
			processFirstPersonModel(
				state,
				state.objects.firstPersonRenderData,
				std::static_pointer_cast<OpenGLRenderContext::FirstPersonRenderData>(context->data)
			);
			break;
		}
		case OpenGLRenderContext::RenderContextType::Effect:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::SceneEffectRenderData>(context->data);
			if (ptr && ptr->properties)
				state.objects.sceneRenderData.effectItems.push_back(ptr->properties);
			break;
		}
		default:
			break;
		}
	}

	if (framedata->skybox)
		state.skybox.cube = framedata->skybox;
}

void WorldManager::processSceneModel(
	RenderState& state,
	OpenGLRenderObjectData::SceneRenderData& renderData,
	const std::shared_ptr<OpenGLRenderContext::SceneModelRenderData>& data
)
{
	using OpaqueMeshItem = OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem;
	using TransparentMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem;

	if (!data || !data->model)
		return;

	auto transform = data->transformView.transformTripleBuffer->acquireReadBuffer();
	auto prevTransform = *data->transformView.prevRenderTransforms;

	*data->transformView.prevRenderTransforms = transform;

	if (data->animatorViews.empty())
	{
		for (auto& info : data->model->getMeshInfos())
		{
			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
			if (isTransprant)
			{
				TransparentMeshItem item;
				item.transform = transform;
				item.prevTransform = prevTransform;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				OpaqueMeshItem item;
				item.transform = transform;
				item.prevTransform = prevTransform;
				item.meshinfo = info;
				renderData.opaqueMesh.push_back(item);

				auto& material = item.meshinfo.material;
				if (material->GetTwoSided())
					state.objects.sceneRenderData.opaqueMesh_renderIndex.twoSideIndex.push_back(renderData.opaqueMesh.size() - 1);
				else
					state.objects.sceneRenderData.opaqueMesh_renderIndex.oneSideIndex.push_back(renderData.opaqueMesh.size() - 1);
			}
		}
	}
	else
	{
		std::vector<MeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<OpenGLRenderContext::AnimatorView>>(data->animatorViews);

		for (auto& info : data->model->getMeshInfos())
		{
			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
			if (isTransprant)
			{
				TransparentSkinnedMeshItem item;
				item.transform = transform;
				item.prevTransform = prevTransform;
				item.meshinfo = info;
				item.animators = shadredAnimatorViews;
				renderData.transparentSkinnedMesh.push_back(item);
			}
			else
			{
				meshInfos.push_back(info);
			}
		}
		if (!meshInfos.empty())
		{
			OpaqueSkinnedModelItem item;
			item.transform = transform;
			item.prevTransform = prevTransform;
			item.models = std::move(meshInfos);
			item.animators = shadredAnimatorViews;
			renderData.opaqueSkinnedModel.push_back(item);
		}
	}
}

void WorldManager::processFirstPersonModel(
	RenderState& state,
	OpenGLRenderObjectData::FirstPersonRenderData& renderData,
	const std::shared_ptr<OpenGLRenderContext::FirstPersonRenderData>& data)
{
	using OpaqueMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::OpaqueMeshItem;
	using TransparentMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::TransparentSkinnedMeshItem;

	if (!data || !data->model)
		return;

	auto cameraView = data->cameraView;

	if (data->animatorViews.empty())
	{
		for (auto& info : data->model->getMeshInfos())
		{
			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
			if (isTransprant)
			{
				TransparentMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				OpaqueMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				renderData.opaqueMesh.push_back(item);
			}
		}
	}
	else
	{
		std::vector<MeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<OpenGLRenderContext::AnimatorView>>(std::move(data->animatorViews));

		for (auto& info : data->model->getMeshInfos())
		{
			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
			if (isTransprant)
			{
				TransparentSkinnedMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				item.animators = shadredAnimatorViews;
				renderData.transparentSkinnedMesh.push_back(item);
			}
			else
			{
				meshInfos.push_back(info);
			}
		}
		if (!meshInfos.empty())
		{
			OpaqueSkinnedModelItem item;
			item.cameraView = cameraView;
			item.models = std::move(meshInfos);
			item.animators = shadredAnimatorViews;
			renderData.opaqueSkinnedModel.push_back(item);
		}
	}
}

void WorldManager::WorldLoop()
{
	int targetfps = (1000.f / std::min(_world->getFixedDeltaTime(), _world->getLogicDeltaTime())) + 1;
	DynamicFpsController fpscontroller(targetfps);
	fpscontroller.reset();

	// 主循环
	while (!_stop)
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
	_stop = true;
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
	return _openglRenderer->GetOption();
}

void WorldManager::SetOption(RenderOption option)
{
	_openglRenderer->SetOption(option);
}

void WorldManager::ResizeOpenGL(uint32_t width, uint32_t height)
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
		if (resizePending && Tool::GetTimestampMilliseconds() - resizeTimeStamp > 200 && _openglRenderer)
		{
			_openglRenderer->Resize(pendingWidth, pendingHeight);
			resizePending = false;
		}
	}
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

std::shared_ptr<OpenGLRenderer> WorldManager::GetOpenGLRener()
{
	return _openglRenderer;
}

std::shared_ptr<World> WorldManager::GetWorld()
{
	return _world;
}

std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> WorldManager::GetTriBuffer()
{
	return _triBuffer;
}
