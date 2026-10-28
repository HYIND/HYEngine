#include "SceneEditor.h"
#include "EngineRuntime.h"
#include "CommonSystems.h"
#include "PlatformRuntimeSystems.h"
#include "ECSCore/World.h"
#include "Helper/DynamicFpsController.h"
#include "Project/ProjectManager.h"

#include "CommonData/LightFactory.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"

#include "Input/EngineInputContext.h"
#include "RenderEngine/Renderer3D.h"

#include "EditorTags.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <stdexcept>
#include <Windows.h>

static std::filesystem::path GetEditorExecutableDirectory()
{
    std::vector<wchar_t> pathBuffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (length == 0 || length >= pathBuffer.size())
        return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(pathBuffer.data(), length)).parent_path();
}

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

        if (auto renderModel = entity.tryGetComponent<RenderStaticModel>(); renderModel && renderModel->model)
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
    entity.addComponent<TageEditorPick>();
}

void SetNameTag(Entity entity, const std::string& name)
{
    if (!entity)
        return;

    if (!entity.hasComponent<TagEditorName>())
    {
        auto& nameTag = entity.addComponent<TagEditorName>();
        nameTag.name = name;
    }
    else
    {
        auto& nameTag = entity.getComponent<TagEditorName>();
        nameTag.name = name;
    }
}

static void GeneralDefaultWorld(World& world)
{
    {
        Entity entity = LightFactory::CreateDirLight(world, glm::vec3(1, -1, 1), glm::vec3(1.0f), 3.5, true, 4, 3000);
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
        static auto skyboxcube = std::make_shared<TextureCube>(faces, TextureConfig::GetDefaultSkyCubeConfig());
        Entity entity = world.createEntity();
        entity.addComponent<SkyBox>(skyboxcube);

        SetNameTag(entity, "DefaultSkybox");
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

        auto& rendermodel = cube.addComponent<RenderStaticModel>();
        rendermodel.model = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

        AddPickProxy(cube);
        SetNameTag(cube, "cube_1");
    }

    {
        Entity entity = world.createEntity();
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

        auto& rendermodel = entity.addComponent<RenderStaticModel>();
        rendermodel.model = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

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

        auto model = RenderHelper::GetSphereModel(0.5, 72, 36, "DefaultSphere");

        auto& rendermodel = sphere.addComponent<RenderStaticModel>();
        rendermodel.model = model;

        if (!model->GetMeshInfos().empty() && model->GetMeshInfos()[0].material)
        {
            auto& material = model->GetMeshInfos()[0].material;
            material->SetMetallic(0.5);
            material->SetRoughness(0.3f);
            material->SetAlbedo(glm::vec3(1.0f));
        }

        AddPickProxy(sphere);
        SetNameTag(sphere, "sphere_1");
    }


    {
        float floorSize = 50.f;
        auto model = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

        auto& meshInfo = model->GetMeshInfos();
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

        auto& rendermodel = floor.addComponent<RenderStaticModel>();
        rendermodel.model = model;

        AddPickProxy(floor);
        SetNameTag(floor, "floor");
    }
}



SceneEditor::SceneEditor(EngineRuntime& runtime)
    : m_runtime(runtime)
{
}

void SceneEditor::RotateCamera(float deltaX, float deltaY)
{
    if (!_world)
        return;
    m_camera.RotateCamera(m_runtime, *_world, deltaX, deltaY);
}

void SceneEditor::PanCamera(float deltaX, float deltaY)
{
    if (!_world)
        return;
    const float viewportHeight = m_viewportHeight ? m_viewportHeight() : 1.0f;
    m_camera.PanCamera(m_runtime, *_world, deltaX, deltaY, viewportHeight);
}

std::shared_ptr<SceneCodec> SceneEditor::CreateDefaultSceneCodec()
{
    World defaultWorld;
    GeneralDefaultWorld(defaultWorld);
    RegisterSceneComponentCodecs();

    auto scene = std::make_shared<SceneCodec>();
    scene->SetName("DefaultScene");
    if (!scene->Serialize(defaultWorld))
        return nullptr;
    return scene;
}

bool SceneEditor::OpenEditorScene(const std::function<bool(World&)>& populateScene)
{
    if (!populateScene)
        return false;

    const bool opened = m_runtime.OpenEmptyScene([&](World& world) {
        // 内置流水线和项目模块系统都由 EngineRuntime 装好了，编辑器只负责填充场景。
        if (!populateScene(world))
            return false;
        world.update(16);
        return true;
        });
    _world = opened ? m_runtime.GetWorld() : nullptr;
    return opened;
}

static void AddFreeCamera(World& world, Entity* outFreeEntity = nullptr, Entity* outCameraEntity = nullptr)
{
    Entity freeEntity = world.createEntityWithTag<TagEditorCamera>();
    freeEntity.addComponent<Transform>();
    freeEntity.addComponent<PlayerInput>();
    auto& controller = freeEntity.addComponent<Controller>();
    controller.yaw = -90.f;
    controller.pitch = -45.f;
    freeEntity.addComponent<TagCurrentControl>();
    auto& tag = freeEntity.addComponent<TagFreeCamera>();
    tag.velocity = 0.25;
    SetNameTag(freeEntity, "editor_FreeEntity");

    Entity cameraEntity = world.createEntity();
    auto& transform = cameraEntity.addComponent<Transform>(glm::vec3(0, 10, 10));
    auto& camera = cameraEntity.addComponent<CameraComponent>();
    camera.fov = 80.0;
    camera.nearPlane = 0.1;
    camera.farPlane = 800.0;
    auto& follow = cameraEntity.addComponent<CameraFollow>();
    follow.target = freeEntity;
    follow.offset = glm::vec3(0.f);
    world.SetMainCamera(cameraEntity);
    SetNameTag(cameraEntity, "editor_FreeCameraEntity");

    if (outFreeEntity)
        *outFreeEntity = freeEntity;
    if (outCameraEntity)
        *outCameraEntity = cameraEntity;
}

void SceneEditor::InitDefaultWorld()
{
    if (_world)
        StopWorld();

    OpenEditorScene([](World& world) {
        GeneralDefaultWorld(world);
        AddFreeCamera(world);
        return true;
        });
    _editorState = EditorWorldState::Editing;
}

// 场景里存的编辑器视角：读不出来就保持默认视角，不影响场景本身。
static void RestoreEditorCamera(const nlohmann::json& settings, Entity editorFreeEntity, Entity editorFreeCameraEntity)
{
    if (!editorFreeEntity || !editorFreeCameraEntity)
        return;
    if (settings.is_object() && settings.contains("editorFreeCamera"))
    {
        const auto& cameraSettings = settings["editorFreeCamera"];

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

        if (cameraSettings.is_object()
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
                    auto& cameraComponent = editorFreeCameraEntity.getComponent<CameraComponent>();
                    auto& follow = editorFreeCameraEntity.getComponent<CameraFollow>();
                    auto& controller = editorFreeEntity.getComponent<Controller>();
                    auto& freeCamera = editorFreeEntity.getComponent<TagFreeCamera>();
                    editorFreeCameraEntity.getComponent<Transform>() = cameraTransform;
                    cameraComponent.fov = fov;
                    cameraComponent.nearPlane = nearPlane;
                    cameraComponent.farPlane = farPlane;
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
}

bool SceneEditor::OpenWithHooks(const std::function<bool(const EngineRuntime::SceneOpenHooks&, std::string*)>& opener)
{
    const bool resumeAfterLoad = _editorState == EditorWorldState::Playing;
    RegisterSceneComponentCodecs();
    if (_world)
        StopWorld();

    // 编辑器专属的相机实体必须在反序列化之前进 World，视角参数在反序列化之后按
    // 场景里存的值恢复：两件事都通过引擎的挂钩做，引擎本身不认识编辑器相机。
    Entity editorFreeEntity, editorFreeCameraEntity;
    EngineRuntime::SceneOpenHooks hooks;
    hooks.prepare = [&](World& world) {
        AddFreeCamera(world, &editorFreeEntity, &editorFreeCameraEntity);
        return true;
        };
    hooks.finish = [&](World&, const SceneCodec& loadedScene) {
        RestoreEditorCamera(loadedScene.GetSettings(), editorFreeEntity, editorFreeCameraEntity);
        };

    std::string error;
    if (!opener(hooks, &error))
    {
        _world = nullptr;
        _editorState = EditorWorldState::Editing;
        return false;
    }

    _world = m_runtime.GetWorld();
    _editorState = EditorWorldState::Editing;
    if (resumeAfterLoad)
        RunWorld();
    return true;
}

bool SceneEditor::LoadSceneAsset(const std::string& sceneAsset)
{
    return OpenWithHooks([this, &sceneAsset](const EngineRuntime::SceneOpenHooks& hooks, std::string* error) {
        return m_runtime.OpenScene(sceneAsset, hooks, error);
        });
}

bool SceneEditor::LoadFromSceneCodec(const std::shared_ptr<SceneCodec>& scene)
{
    return OpenWithHooks([this, &scene](const EngineRuntime::SceneOpenHooks& hooks, std::string* error) {
        return m_runtime.OpenScene(scene, hooks, error);
        });
}

void SceneEditor::RegisterSceneComponentCodecs()
{

    NComponentCodec::ComponentEditorBehavior editorPickBehavior;
    editorPickBehavior.visible = false;
    editorPickBehavior.addable = false;
    editorPickBehavior.removable = false;
    NComponentCodec::RegisterComponentCodec<TageEditorPick>(
        "editor.pick",
        "Editor",
        [](const TageEditorPick&) { return nlohmann::json::object(); },
        [](TageEditorPick&, const nlohmann::json& data) { return data.is_object(); },
        {}, {}, {}, 1, std::move(editorPickBehavior));

    NComponentCodec::RegisterComponentCodec<TagEditorName>(
        "editor.name",
        "EditorNameTag",
        [](const TagEditorName& tag) { return nlohmann::json{ {"name", tag.name} }; },
        [](TagEditorName& tag, const nlohmann::json& data) {
            if (!data.is_object() || !data.contains("name") || !data["name"].is_string())
                return false;
            tag.name = data["name"].get<std::string>();
            return true;
        },
        [](const TagEditorName& tag) { return tag.name; },
        [](TagEditorName& tag, const std::string& name) { tag.name = name; });
}

void SceneEditor::RunWorld()
{
    if (!_world || _world->isRunning())
        return;

    // 播放态的场景线程归引擎：编辑器只管“开始播放/停止播放”这个状态转换。
    m_runtime.StartSceneThread(1000.f / 165.f, 1000.f / 60.f);
    _editorState = EditorWorldState::Playing;
}

bool SceneEditor::PlayWorld()
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

    static auto IsEditorEntity = [](Entity entity) {
        if (auto follow = entity.tryGetComponent<CameraFollow>())
        {
            if (follow->target && follow->target.hasComponent<TagEditorCamera>())
                return true;
        }
        return entity.hasComponent<TagEditorCamera>();
        };

    auto snapshot = std::make_shared<SceneCodec>();
    if (!snapshot->Serialize(*_world, [&](Entity entity) {
        return !IsEditorEntity(entity);
        }))
        return false;

    const auto entities = _world->getAllEntity();
    for (Entity cameraEntity : entities)
    {
        if (!cameraEntity.hasComponents<CameraComponent, Transform, CameraFollow>())
            continue;
        Entity freeEntity = cameraEntity.getComponent<CameraFollow>().target;
        if (!freeEntity || !freeEntity.hasComponents<TagFreeCamera, TagCurrentControl, Transform, PlayerInput, Controller>())
            continue;

        const auto& camera = cameraEntity.getComponent<CameraComponent>();
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
            {"fov", camera.fov},
            {"nearPlane", camera.nearPlane},
            {"farPlane", camera.farPlane},
            {"offset", { follow.offset.x, follow.offset.y, follow.offset.z }}
        };
        break;
    }

    _playSnapshot = std::move(snapshot);

    bool findSceneCamera = false;
    auto cameraEntities = _world->getEntitiesWith<Transform, CameraComponent>();
    for (auto& entity : cameraEntities)
    {
        if (IsEditorEntity(entity))
            continue;
        _world->SetMainCamera(entity);
        findSceneCamera = true;
    }

    if (findSceneCamera)
    {
        std::vector<Entity> toDestory;
        const auto entities = _world->getAllEntity();
        for (Entity entity : entities)
        {
            if (IsEditorEntity(entity))
                toDestory.push_back(entity);
        }

        for (auto& entity : toDestory)
            _world->destroyEntity(entity);
    }

    RunWorld();
    return true;
}

void SceneEditor::StopPlayback()
{
    if (_editorState == EditorWorldState::Editing)
        return;

    m_runtime.StopSceneThread();
    _editorState = EditorWorldState::Editing;
    if (_playSnapshot)
    {
        // 快照回滚也走引擎的场景入口：资源解析仍然由工程资产库负责。
        auto snapshot = std::move(_playSnapshot);
        LoadFromSceneCodec(snapshot);
    }
}

void SceneEditor::UpdateEditorFrame(ProjectManager* projectManager)
{
	if (!_world || (_editorState != EditorWorldState::Editing && _editorState != EditorWorldState::Paused))
		return;

    if (_editorState == EditorWorldState::Editing && projectManager)
    {
        static auto lastScriptCheck = Tool::GetTimestampMilliseconds();
        if (auto now = Tool::GetTimestampMilliseconds(); now - lastScriptCheck >= 500)
        {
            auto changed = projectManager->CheckScriptAssetChanges();
            if (!changed.empty())
                ReloadScriptAssets(changed);

            auto changedUI = projectManager->CheckUIDocumentChanges();
            if (!changedUI.empty())
                ReloadUIAssets();

            lastScriptCheck = Tool::GetTimestampMilliseconds();
        }
    }

    m_runtime.StepEditorFrame(1000.f / 60.f);
}

EditorWorldState SceneEditor::GetEditorState() const
{
    return _editorState;
}

void SceneEditor::PauseWorld()
{
    if (!_world || _editorState != EditorWorldState::Playing)
        return;
    m_runtime.StopSceneThread();
    _editorState = EditorWorldState::Paused;
}

void SceneEditor::ContinueWorld()
{
    if (!_world || _editorState != EditorWorldState::Paused)
        return;
    RunWorld();
}

void SceneEditor::StopWorld()
{
    m_runtime.StopSceneThread();
    m_runtime.CloseScene();
    _world = nullptr;
    _playSnapshot.reset();
    _editorState = EditorWorldState::Editing;
}

Entity SceneEditor::PickObject(const glm::vec3& origin, const glm::vec3& direction)
{
    auto result = RayCast(origin, direction);
    if (result.hit && result.hitEntity.hasComponents<Transform, TageEditorPick>())
        return result.hitEntity;
    return Entity();
}

RaycastHit SceneEditor::RayCast(const glm::vec3& origin, const glm::vec3& direction)
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

void SceneEditor::SetInputActive(bool enable)
{
    if (auto inputContext = m_runtime.GetInputContext())
        inputContext->SetActive(enable);
}

Entity SceneEditor::CreateModelEntity(std::shared_ptr<StaticModel> model)
{
    if (!model)
        return Entity();

    Entity entity;
    _world->SubmitCommand([&] {
        entity = _world->createEntity();
        auto& trans = entity.addComponent<Transform>();
        trans.position = glm::vec3(0, 0, 0);
        trans.rotation = glm::identity<glm::quat>();

        auto& rendermodel = entity.addComponent<RenderStaticModel>();
        rendermodel.model = model;

        AddPickProxy(entity);
        SetNameTag(entity, std::format("model_entity_{}", entity.getId()));
        }).get();

    return entity;
}

bool SceneEditor::DuplicateEntity(Entity oriEntity, Entity& newEntity)
{
    auto world = _world;
    if (!_world || (!_world->isRunning() && _editorState != EditorWorldState::Editing))
        return false;

    bool isSuccess = false;
    world->SubmitCommand([&isSuccess, &newEntity, entity = oriEntity, world = world]() -> void {
        if (!entity || !world)
        {
            isSuccess = false;
            return;
        }

        newEntity = world->DuplicateEntity(entity);
        if (newEntity.hasComponent<TagEditorName>())
        {
            auto& tag = newEntity.getComponent<TagEditorName>();
            tag.name += "_copy";
        }
        if (auto renderModel = newEntity.tryGetComponent<RenderStaticModel>(); renderModel && renderModel->model)
            renderModel->model = renderModel->model->Clone(true, true);

        isSuccess = true;
        }
    ).get();

    return isSuccess && newEntity;
}

void SceneEditor::AddLight(LightType type, const glm::vec3& position, const std::string& name)
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

void SceneEditor::AddGeometry(GeometryType type, const glm::vec3& position, const std::string& name)
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

            auto& rendermodel = entity.addComponent<RenderStaticModel>();
            rendermodel.model = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "DefaultCube");

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

            auto& rendermodel = entity.addComponent<RenderStaticModel>();
            rendermodel.model = RenderHelper::GetSphereModel(0.5, 72, 36, "DefaultSphere");

            AddPickProxy(entity);
            if (!name.empty())
                SetNameTag(entity, name);
            });
    }
}

void SceneEditor::AddEmptyEntity(const glm::vec3& position, const std::string& name)
{
    _world->SubmitCommand([&, position = position, name = name](World& world)-> void {
        Entity entity = world.createEntity();
        auto& trans = entity.addComponent<Transform>();
        trans.position = position;

        AddPickProxy(entity);
        if (!name.empty())
            SetNameTag(entity, name);
        });
}

void SceneEditor::ReloadUIAssets()
{
    if (!_world)
        return;

    auto apply = [](World& world) {
        for (auto [entity, view] : world.getViewWith<UIView>())
            view.needReload = true;
        };

    if (_world->isRunning())
        _world->SubmitCommand(apply);
    else
        apply(*_world);
}

void SceneEditor::ReloadScriptAssets(const std::vector<std::shared_ptr<ScriptSourceData>>& changed)
{
    if (!_world || changed.empty())
        return;

    auto apply = [changed](World& world) {
        for (auto [entity, script] : world.getViewWith<Script>())
        {
            if (!script.data || script.data->scriptGuid.empty())
                continue;
            for (auto& data : changed)
            {
                if (data->scriptGuid == script.data->scriptGuid)
                {
                    script.data = data;
                    script.needReload = true;
                    break;
                }
            }
        }
        };

    if (_world->isRunning())
        _world->SubmitCommand(apply);
    else
        apply(*_world);
}

World* SceneEditor::GetWorld()
{
    return m_runtime.GetWorld();
}
