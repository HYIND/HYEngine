#include "WorldManager.h"
#include "CommonSystems.h"
#include "GamePlaySystems.h"
#include "GameRuntimeSystems.h"
#include "ECSCore/World.h"
#include "Helper/DynamicFpsController.h"
#include "Project/ProjectManager.h"

#include "CommonData/LightFactory.h"
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

void LoadDefaultScene(WorldManager* manager)
{
	auto world = manager->GetWorld();

	{
		Entity entity = LightFactory::CreateDirLight(*world, glm::vec3(1, -1, 1), glm::vec3(1.0f), 3.5, true, 4, 3000);
		AddPickProxy(entity);
		SetNameTag(entity, "DirLight");
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
		auto skyboxcube = std::make_shared<TextureCube>(faces, TextureConfig::GetDefaultSkyCubeConfig());
		Entity entity = world->createEntityWithTag<TagSkyBox>();
		entity.addComponent<SkyBox>(skyboxcube);

		SetNameTag(entity, "DefaultSkybox");
	}

	{
		Entity cube = world->createEntity();
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
		rendermodel.model = GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

		AddPickProxy(cube);
		SetNameTag(cube, "cube_1");
	}

	{
		Entity entity = world->createEntity();
		auto& trans = entity.addComponent<Transform>();
		trans.position = { -3,3,0 };

		auto& physics = entity.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.mass = 5.f;
		physics.collisionShape.AddBoxShape();

		auto& rendermodel = entity.addComponent<RenderModel>();
		rendermodel.model = GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

		AddPickProxy(entity);
		SetNameTag(entity, "cube_script_test");

		static auto ReadFromFile = [](const std::string& path) ->std::string
			{
				std::ifstream file;
				file.exceptions(std::ifstream::badbit);// 异常机制处理：保证ifstream对象可以抛出异常：

				try
				{
					file.open(path);
					if (!file) {
						std::cerr << "文件打开失败" << ",path=" << path << std::endl;
						return std::string();
					}

					std::stringstream stream;
					stream << file.rdbuf();
					file.close();

					return stream.str();
				}
				catch (std::ifstream::failure e) {
					std::cout << "ERROR::FILE_NOT_SUCCESSFULLY_READ" << std::endl;
				}

				return std::string();
			};

		auto& script = entity.addComponent<Script>(ReadFromFile("Test/script/TestScript.lua"));
	}

	{
		float radius = 3.0;

		Entity sphere = world->createEntity();
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

		auto model = GetSphereModel(0.5, 72, 36, "DefaultSphere");

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
		SetNameTag(sphere, "sphere_1");
	}


	{
		float floorSize = 50.f;
		auto model = GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 1, 1));

		Entity floor = world->createEntity();
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

		AddPickProxy(floor);
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

void WorldManager::CreateWorld(bool loadDefaultScene)
{
	auto world = std::make_shared<World>();

	// 通过World注册系统
	auto& scriptSystem = world->registerSystem<ScriptSystem>();
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
	scriptSystem.setPriority(450);
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

	_world = world;

	if (loadDefaultScene)
		LoadDefaultScene(this);

	_world->update(16);
}

static void AddFreeCamera(World& world)
{
	Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
	freeEntity.addComponent<Transform>();
	freeEntity.addComponent<PlayerInput>();
	auto& controller = freeEntity.addComponent<Controller>();
	controller.yaw = -90.f;
	controller.pitch = -45.f;
	freeEntity.addComponent<TagCurrentControl>();
	freeEntity.getComponent<TagFreeCamera>().velocity = 0.25f;
	SetNameTag(freeEntity, "freeEntity");

	Entity cameraEntity = world.createEntityWithTag<TagCamera>();
	auto& transform = cameraEntity.addComponent<Transform>(glm::vec3(0, 10, 10));
	auto& camera = cameraEntity.addComponent<CameraComponent>();
	camera.camera.SetFOV(80.f);
	camera.camera.SetNearPlane(0.1f);
	camera.camera.SetFarPlane(800.f);
	camera.SetTransForm(transform);
	auto& follow = cameraEntity.addComponent<CameraFollow>();
	follow.target = freeEntity;
	follow.offset = glm::vec3(0.f);
	world.SetMainCamera(cameraEntity);
	SetNameTag(cameraEntity, "freeCameraEntity");
}

void WorldManager::InitDefaultWorld()
{
	if (_world || _worldThread)
		StopWorld();

	CreateWorld(true);
	auto world = _world;
	if (!world)
		return;

	AddFreeCamera(*world);
	_editorState = EditorWorldState::Editing;
}

bool WorldManager::LoadFromSceneCodec(const std::shared_ptr<SceneCodec>& scene, ProjectManager* projectManager)
{
	if (!scene)
		return false;
	const bool resumeAfterLoad = _editorState == EditorWorldState::Playing;
	RegisterSceneComponentCodecs();
	if (_world || _worldThread)
		StopWorld();

	CreateWorld(false);
	auto world = _world;
	if (!world)
		return false;

	AddFreeCamera(*world);

	auto resolveScriptAsset = [projectManager](const AssetGUID& guid) -> std::shared_ptr<ScriptSourceData> {
		if (!projectManager)
			return nullptr;
		auto database = projectManager->GetDatabase();
		auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
		if (!meta || meta->type != AssetType::Script)
			return nullptr;
		auto asset = projectManager->LoadAsset(guid);
		return asset ? asset->AsScript() : nullptr;
		};
	auto resolveModelAsset = [projectManager](const AssetGUID& guid) -> std::shared_ptr<Model> {
		if (guid == "DefaultCube")
			return GetCubeModel(glm::vec3(0.5f), 1.0f, guid);
		if (guid == "DefaultSphere")
			return GetSphereModel(0.5f, 72, 36, guid);
		if (!projectManager)
			return nullptr;

		auto database = projectManager->GetDatabase();
		auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
		if (!meta)
			return nullptr;

		auto asset = projectManager->LoadAsset(guid);
		if (!asset)
			return nullptr;
		if (meta->type == AssetType::Model)
			return asset->AsModel();
		if (meta->type != AssetType::StaticMesh)
			return nullptr;

		auto meshes = asset->AsStaticMesh();
		if (meshes.empty())
			return nullptr;
		auto model = std::make_shared<Model>();
		for (auto& mesh : meshes)
			model->AddMesh(mesh, std::make_shared<Material>());
		model->SetAssetGUID(guid);
		return model;
		};
	if (!scene->Deserialize(*world, resolveScriptAsset, resolveModelAsset))
		return false;

	const auto& settings = scene->GetSettings();
	if (settings.is_object() && settings.contains("editorFreeCamera"))
	{
		const auto& cameraSettings = settings["editorFreeCamera"];
		const auto entities = world->getAllEntity();
		const auto cameraEntity = std::find_if(entities.begin(), entities.end(), [](Entity entity) {
			if (!entity.hasComponents<TagCamera, CameraComponent, Transform, CameraFollow>())
				return false;
			const Entity target = entity.getComponent<CameraFollow>().target;
			return target && target.hasComponents<TagFreeCamera, TagCurrentControl, Transform, PlayerInput, Controller>();
			});
		const auto isFiniteArray = [](const nlohmann::json& value, size_t size) {
			if (!value.is_array() || value.size() != size)
				return false;
			for (const auto& element : value)
				if (!element.is_number() || !std::isfinite(element.get<double>()))
					return false;
			return true;
			};
		const auto readVec3 = [&isFiniteArray](const nlohmann::json& value, glm::vec3& result) {
			if (!isFiniteArray(value, 3))
				return false;
			result = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
			};
		const auto readTransform = [&readVec3, &isFiniteArray](const nlohmann::json& data, Transform& transform) {
			if (!data.is_object() || !data.contains("position") || !data.contains("rotation") || !data.contains("scale"))
				return false;
			glm::vec3 position;
			glm::vec3 scale;
			const auto& rotation = data["rotation"];
			if (!readVec3(data["position"], position) || !readVec3(data["scale"], scale)
				|| !isFiniteArray(rotation, 4))
				return false;
			glm::quat quaternion(
				rotation[0].get<float>(), rotation[1].get<float>(),
				rotation[2].get<float>(), rotation[3].get<float>());
			const float length = glm::length(quaternion);
			if (!std::isfinite(length) || length <= 1e-6f)
				return false;
			transform.position = position;
			transform.rotation = glm::normalize(quaternion);
			transform.scale = scale;
			return true;
			};
		if (cameraSettings.is_object() && cameraEntity != entities.end()
			&& cameraSettings.contains("cameraTransform")
			&& cameraSettings.contains("yaw") && cameraSettings["yaw"].is_number()
			&& cameraSettings.contains("pitch") && cameraSettings["pitch"].is_number()
			&& cameraSettings.contains("velocity") && cameraSettings["velocity"].is_number()
			&& cameraSettings.contains("offset") && isFiniteArray(cameraSettings["offset"], 3)
			&& cameraSettings.contains("fov") && cameraSettings["fov"].is_number()
			&& cameraSettings.contains("nearPlane") && cameraSettings["nearPlane"].is_number()
			&& cameraSettings.contains("farPlane") && cameraSettings["farPlane"].is_number())
		{
			try
			{
				Transform cameraTransform;
				const bool validTransform = readTransform(cameraSettings["cameraTransform"], cameraTransform);
				const float fov = cameraSettings["fov"].get<float>();
				const float nearPlane = cameraSettings["nearPlane"].get<float>();
				const float farPlane = cameraSettings["farPlane"].get<float>();
				const float yaw = cameraSettings["yaw"].get<float>();
				const float pitch = cameraSettings["pitch"].get<float>();
				const float velocity = cameraSettings["velocity"].get<float>();
				glm::vec3 offset;
				const bool validOffset = readVec3(cameraSettings["offset"], offset);
				if (validTransform && validOffset
					&& std::isfinite(fov) && fov > 0.f && fov < 180.f
					&& std::isfinite(nearPlane) && nearPlane > 0.f
					&& std::isfinite(farPlane) && farPlane > nearPlane
					&& std::isfinite(yaw) && std::isfinite(pitch) && pitch >= -89.f && pitch <= 89.f
					&& std::isfinite(velocity) && velocity >= 0.f)
				{
					auto& cameraComponent = cameraEntity->getComponent<CameraComponent>();
					auto& follow = cameraEntity->getComponent<CameraFollow>();
					Entity freeEntity = follow.target;
					auto& controller = freeEntity.getComponent<Controller>();
					auto& freeCamera = freeEntity.getComponent<TagFreeCamera>();
					cameraEntity->getComponent<Transform>() = cameraTransform;
					cameraComponent.camera.SetPosition(cameraTransform.position);
					cameraComponent.camera.SetFOV(fov);
					cameraComponent.camera.SetNearPlane(nearPlane);
					cameraComponent.camera.SetFarPlane(farPlane);
					cameraComponent.camera.SetYawPitch(yaw, pitch);
					controller.yaw = yaw;
					controller.pitch = pitch;
					freeCamera.velocity = velocity;
					follow.offset = offset;
				}
			}
			catch (const nlohmann::json::exception&)
			{
			}
		}
	}

	world->update(16);
	_editorState = EditorWorldState::Editing;
	if (resumeAfterLoad)
		RunWorld();
	return true;
}

void WorldManager::RegisterSceneComponentCodecs()
{
	NComponentCodec::RegisterComponentCodec<EditorPick>(
		"editor.pick",
		[](const EditorPick&) { return nlohmann::json::object(); },
		[](EditorPick&, const nlohmann::json& data) { return data.is_object(); });

	NComponentCodec::RegisterComponentCodec<NameTag>(
		"editor.name",
		[](const NameTag& tag) { return nlohmann::json{ {"name", tag.name} }; },
		[](NameTag& tag, const nlohmann::json& data) {
			if (!data.is_object() || !data.contains("name") || !data["name"].is_string())
				return false;
			tag.name = data["name"].get<std::string>();
			return true;
		},
		[](const NameTag& tag) { return tag.name; },
		[](NameTag& tag, const std::string& name) { tag.name = name; });
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
	_editorState = EditorWorldState::Playing;
}

bool WorldManager::PlayWorld()
{
	if (!_world || _editorState == EditorWorldState::Playing)
		return false;

	if (_editorState == EditorWorldState::Paused)
	{
		RunWorld();
		return true;
	}

	RegisterSceneComponentCodecs();
	if (_world->isRunning())
		_world->stop();
	auto snapshot = std::make_shared<SceneCodec>();
	if (!snapshot->Serialize(*_world, [](Entity entity) {
		return !entity.hasComponent<TagFreeCamera>() && !entity.hasComponent<TagCamera>();
		}))
		return false;

	const auto entities = _world->getAllEntity();
	for (Entity cameraEntity : entities)
	{
		if (!cameraEntity.hasComponents<TagCamera, CameraComponent, Transform, CameraFollow>())
			continue;
		Entity freeEntity = cameraEntity.getComponent<CameraFollow>().target;
		if (!freeEntity || !freeEntity.hasComponents<TagFreeCamera, TagCurrentControl, Transform, PlayerInput, Controller>())
			continue;

		const auto& camera = cameraEntity.getComponent<CameraComponent>().camera;
		const auto& cameraTransform = cameraEntity.getComponent<Transform>();
		const auto& follow = cameraEntity.getComponent<CameraFollow>();
		const auto& controller = freeEntity.getComponent<Controller>();
		const auto& freeCamera = freeEntity.getComponent<TagFreeCamera>();
		snapshot->GetSettings()["editorFreeCamera"] = {
			{"cameraTransform", {
				{"position", { cameraTransform.position.x, cameraTransform.position.y, cameraTransform.position.z }},
				{"rotation", { cameraTransform.rotation.w, cameraTransform.rotation.x, cameraTransform.rotation.y, cameraTransform.rotation.z }},
				{"scale", { cameraTransform.scale.x, cameraTransform.scale.y, cameraTransform.scale.z }}
			}},
			{"yaw", controller.yaw},
			{"pitch", controller.pitch},
			{"velocity", freeCamera.velocity},
			{"fov", camera.GetFOV()},
			{"nearPlane", camera.GetNearPlane()},
			{"farPlane", camera.GetFarPlane()},
			{"offset", { follow.offset.x, follow.offset.y, follow.offset.z }}
		};
		break;
	}

	_playSnapshot = std::move(snapshot);
	RunWorld();
	return true;
}

void WorldManager::StopPlayback(ProjectManager* projectManager)
{
	if (_editorState == EditorWorldState::Editing)
		return;

	StopWorldThread();
	_editorState = EditorWorldState::Editing;
	if (_playSnapshot)
	{
		auto snapshot = std::move(_playSnapshot);
		LoadFromSceneCodec(snapshot, projectManager);
	}
}

void WorldManager::StopWorldThread()
{
	_worldStop = true;
	if (_worldThread)
	{
		if (_worldThread->joinable())
			_worldThread->join();
		_worldThread.reset();
	}
	if (_world)
	{
		_world->stop();
		_world->ProcessCommands();
	}
}

void WorldManager::UpdateEditorFrame()
{
	if (!_world || _editorState == EditorWorldState::Playing)
		return;

	_world->ProcessCommands();
	const float deltaTime = 1000.f / 60.f;
	if (_editorState == EditorWorldState::Editing)
	{
		if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
			physicsSystem->fixedUpdate(0.f);
		for (Entity entity : _world->getAllEntity())
		{
			if (!entity.hasComponents<TagCurrentControl, TagFreeCamera, PlayerInput, Controller>())
				continue;
			auto& input = entity.getComponent<PlayerInput>();
			auto& controller = entity.getComponent<Controller>();
			if (FocusManager::Instance()->ShouldProcessInput())
			{
				input.setInput(PlayerInput::FORWARD, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Forward)) & 0x8000);
				input.setInput(PlayerInput::BACKWARD, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Backward)) & 0x8000);
				input.setInput(PlayerInput::LEFT, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Left)) & 0x8000);
				input.setInput(PlayerInput::RIGHT, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Right)) & 0x8000);
				input.setInput(PlayerInput::UP, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Up)) & 0x8000);
				input.setInput(PlayerInput::DOWN, GetAsyncKeyState(KeyMapManager::Instance()->GetKeyCode(Keyboard::Down)) & 0x8000);
				MouseManager::Instance()->GetDelta(input.mouseDeltaX, input.mouseDeltaY);
			}
			else
				input.reset();

			controller.setForwardBackward(input.isPressed(PlayerInput::FORWARD) == input.isPressed(PlayerInput::BACKWARD)
				? Controller::ForwardBackward::NONE
				: input.isPressed(PlayerInput::FORWARD) ? Controller::ForwardBackward::FORWARD : Controller::ForwardBackward::BACKWARD);
			controller.setLeftRight(input.isPressed(PlayerInput::LEFT) == input.isPressed(PlayerInput::RIGHT)
				? Controller::LeftRight::NONE
				: input.isPressed(PlayerInput::LEFT) ? Controller::LeftRight::LEFT : Controller::LeftRight::RIGHT);
			controller.setUpDown(input.isPressed(PlayerInput::UP) == input.isPressed(PlayerInput::DOWN)
				? Controller::UpDown::NONE
				: input.isPressed(PlayerInput::UP) ? Controller::UpDown::UP : Controller::UpDown::DOWN);
			controller.addMouseDelta(input.mouseDeltaX, input.mouseDeltaY);
			input.reset();
		}
		if (auto* cameraSystem = _world->getSystem<CameraFollowSystem>())
			cameraSystem->postUpdate(deltaTime);
	}
	if (auto* renderSystem = _world->getSystem<RenderSystem>())
		renderSystem->postUpdate(deltaTime);
}

EditorWorldState WorldManager::GetEditorState() const
{
	return _editorState;
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
	uint32_t targetfps = (1000.f / std::min(_world->getFixedDeltaTime(), _world->getLogicDeltaTime())) + 1;
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
	if (!_world || _editorState != EditorWorldState::Playing)
		return;
	StopWorldThread();
	_editorState = EditorWorldState::Paused;
}

void WorldManager::ContinueWorld()
{
	if (!_world || _editorState != EditorWorldState::Paused)
		return;
	RunWorld();
}

void WorldManager::StopWorld()
{
	StopWorldThread();
	_world.reset();
	_playSnapshot.reset();
	_editorState = EditorWorldState::Editing;
}

Entity WorldManager::PickObject(const glm::vec3& origin, const glm::vec3& direction)
{
	auto result = RayCast(origin, direction);
	if (result.hit && result.hitEntity.hasComponents<Transform, EditorPick>())
		return result.hitEntity;
	return Entity();
}

RaycastHit WorldManager::RayCast(const glm::vec3& origin, const glm::vec3& direction)
{
	if (_world->isRunning())
	{
		RaycastHit result;
		auto query = [&]() {
			if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
				result = physicsSystem->raycast(origin, origin + direction * (2000.f));
			};

		_world->SubmitCommand(query).get();
		return result;
	}
	else
	{
		if (auto* physicsSystem = _world->getSystem<PhysicsSystem>())
			return physicsSystem->raycast(origin, origin + direction * (2000.f));
		return RaycastHit();
	}
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

bool WorldManager::ResizeVulkan(uint32_t width, uint32_t height, std::function<void()> prevClear)
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
		if (resizePending && Tool::GetTimestampMilliseconds() - resizeTimeStamp > 500 && _renderer)
		{
			prevClear();
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

void WorldManager::AddLight(LightType type, const glm::vec3& position, const std::string& name)
{
	if (type == LightType::Directional)
	{
		_world->SubmitCommand([&, position = position, name = name](World& world)-> void {
			Entity entity = LightFactory::CreateDirLight(world, glm::vec3(0, -1, 0));
			if (auto* trans = entity.tryGetComponent<Transform>())
				trans->position = position;
			AddPickProxy(entity);
			if (!name.empty())
				SetNameTag(entity, name);
			});
	}
	else if (type == LightType::Point)
	{
		_world->SubmitCommand([&, position = position, name = name](World& world)-> void {
			Entity entity = LightFactory::CreatePointLight(world, position);
			if (auto* trans = entity.tryGetComponent<Transform>())
				trans->position = position;
			AddPickProxy(entity);
			});
	}
	else if (type == LightType::Spot)
	{
		_world->SubmitCommand([&, position = position, name = name](World& world)-> void {
			Entity entity = LightFactory::CreateSpotLight(world, position, glm::vec3(0, -1, 0));
			if (auto* trans = entity.tryGetComponent<Transform>())
				trans->position = position;
			AddPickProxy(entity);
			if (!name.empty())
				SetNameTag(entity, name);
			});
	}
}

void WorldManager::AddGeometry(GeometryType type, const glm::vec3& position, const std::string& name)
{
	if (type == GeometryType::Cube)
	{
		_world->SubmitCommand([&, position = position, name = name](World& world)-> void {
			Entity entity = world.createEntity();
			auto& trans = entity.addComponent<Transform>();
			trans.position = position;

			auto& physics = entity.addComponent<Physics>();
			physics.bodyType = Physics::BodyType::Static;
			physics.isSensor = false;
			physics.isBullet = true;
			physics.friction = 0.1;
			physics.restitution = 0.8;
			physics.mass = 5.f;
			physics.collisionShape.AddBoxShape();

			auto& rendermodel = entity.addComponent<RenderModel>();
			rendermodel.model = GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

			AddPickProxy(entity);
			if (!name.empty())
				SetNameTag(entity, name);
			});
	}
	else if (type == GeometryType::Sphere)
	{
		_world->SubmitCommand([&, position = position, name = name](World& world)-> void {

			Entity entity = world.createEntity();
			auto& trans = entity.addComponent<Transform>();
			trans.position = position;

			auto& physics = entity.addComponent<Physics>();
			physics.bodyType = Physics::BodyType::Static;
			physics.isSensor = false;
			physics.isBullet = true;
			physics.friction = 0.1;
			physics.restitution = 0.8;
			physics.mass = 5.f;
			physics.collisionShape.AddSphereShape(0.5f);

			auto& rendermodel = entity.addComponent<RenderModel>();
			rendermodel.model = GetSphereModel(0.5, 72, 36, "DefaultSphere");

			AddPickProxy(entity);
			if (!name.empty())
				SetNameTag(entity, name);
			});
	}
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
