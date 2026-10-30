#include "EngineRuntime.h"
#include "Audio/Systems/AudioSystem.h"
#include "Input/EngineInputContext.h"
#include "Input/Systems/LocalInputSystem.h"
#include "Input/Components/PlayerInput.h"
#include "Input/ControllerInput.h"
#include "Components/Controller.h"
#include "CommonTags.h"
#include "Helper/DynamicFpsController.h"
#include "ECSCore/World.h"
#include "EngineSystemPriority.h"
#include "CommonSystems.h"
#include "PlatformRuntimeSystems.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKCore/VulkanInstance.h"
#include "VulkanRenderEngine/VKCore/VulkanDevice.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "Project/ProjectManager.h"
#include "Project/AssetDatabase.h"
#include "Project/AssetObject.h"
#include "VulkanRenderEngine/Base/Skeleton.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "SceneCodec/SceneCodec.h"
#include "Systems/UIViewSystem.h"
#include "UI/EngineCommands.h"
#include "UI/UiTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace
{
	NComponentCodec::ModuleHost ToModuleHost(EngineHostType kind)
	{
		switch (kind)
		{
		case EngineHostType::Editor: return NComponentCodec::ModuleHost::Editor;
		case EngineHostType::Game:   return NComponentCodec::ModuleHost::Game;
		case EngineHostType::Server: return NComponentCodec::ModuleHost::Server;
		}
		return NComponentCodec::ModuleHost::Editor;
	}
}

namespace
{
	constexpr const char* kSceneRenderOptionKey = "renderOption";

	template <typename T>
	void ReadOptionField(const nlohmann::json& json, const char* key, T& out)
	{
		const auto value = json.find(key);
		if (value == json.end() || value->is_null())
			return;
		try
		{
			out = value->get<T>();
		}
		catch (const nlohmann::json::exception&)
		{
		}
	}

	void ReadOptionVec3(const nlohmann::json& json, const char* key, glm::vec3& out)
	{
		const auto value = json.find(key);
		if (value == json.end() || !value->is_array() || value->size() < 3)
			return;
		for (nlohmann::json::size_type index = 0; index < 3; ++index)
		{
			const auto& component = (*value)[index];
			if (component.is_number())
				out[static_cast<int>(index)] = component.get<float>();
		}
	}
}

void to_json(nlohmann::json& json, const Scene3DRenderOption::ExponentialHeightFogParams& params)
{
	json = nlohmann::json{
		{"fogAlbedo", {params.fogAlbedo.x, params.fogAlbedo.y, params.fogAlbedo.z}},
		{"globalDensity", params.globalDensity},
		{"fogStartHeight", params.fogStartHeight},
		{"fogStartDistance", params.fogStartDistance},
		{"fogHeightFalloff", params.fogHeightFalloff},
		{"volumeFogOn", params.volumeFogOn},
		{"fogMaxOpacity", params.fogMaxOpacity},
		{"VolumeLightScatterPathSampleCount", params.VolumeLightScatterPathSampleCount}
	};
}

void from_json(const nlohmann::json& json, Scene3DRenderOption::ExponentialHeightFogParams& params)
{
	ReadOptionVec3(json, "fogAlbedo", params.fogAlbedo);
	ReadOptionField(json, "globalDensity", params.globalDensity);
	ReadOptionField(json, "fogStartHeight", params.fogStartHeight);
	ReadOptionField(json, "fogStartDistance", params.fogStartDistance);
	ReadOptionField(json, "fogHeightFalloff", params.fogHeightFalloff);
	ReadOptionField(json, "volumeFogOn", params.volumeFogOn);
	ReadOptionField(json, "fogMaxOpacity", params.fogMaxOpacity);
	ReadOptionField(json, "VolumeLightScatterPathSampleCount", params.VolumeLightScatterPathSampleCount);
}

void to_json(nlohmann::json& json, const Scene3DRenderOption::AtmosphereParams& params)
{
	json = nlohmann::json{
		{"CubeCapturePosition", {params.CubeCapturePosition.x, params.CubeCapturePosition.y, params.CubeCapturePosition.z}},
		{"PlanetRadius", params.PlanetRadius},
		{"AtmosphereHeight", params.AtmosphereHeight},
		{"RayleighScatteringScalarHeight", params.RayleighScatteringScalarHeight},
		{"MieScatteringScalarHeight", params.MieScatteringScalarHeight},
		{"MieAnisotropy", params.MieAnisotropy},
		{"OzoneLevelCenterHeight", params.OzoneLevelCenterHeight},
		{"OzoneLevelWidth", params.OzoneLevelWidth},
		{"ScatterPathSampleCount", params.ScatterPathSampleCount},
		{"TransmittanceSampleCount", params.TransmittanceSampleCount}
	};
}

void from_json(const nlohmann::json& json, Scene3DRenderOption::AtmosphereParams& params)
{
	ReadOptionVec3(json, "CubeCapturePosition", params.CubeCapturePosition);
	ReadOptionField(json, "PlanetRadius", params.PlanetRadius);
	ReadOptionField(json, "AtmosphereHeight", params.AtmosphereHeight);
	ReadOptionField(json, "RayleighScatteringScalarHeight", params.RayleighScatteringScalarHeight);
	ReadOptionField(json, "MieScatteringScalarHeight", params.MieScatteringScalarHeight);
	ReadOptionField(json, "MieAnisotropy", params.MieAnisotropy);
	ReadOptionField(json, "OzoneLevelCenterHeight", params.OzoneLevelCenterHeight);
	ReadOptionField(json, "OzoneLevelWidth", params.OzoneLevelWidth);
	ReadOptionField(json, "ScatterPathSampleCount", params.ScatterPathSampleCount);
	ReadOptionField(json, "TransmittanceSampleCount", params.TransmittanceSampleCount);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceGeneralParams,
	maxDistance, maxCacheClearDistance)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceReflectParams,
	tMin, tMax, maxBounceLimit, NumSamples, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceGIParams,
	tMin, tMax, maxBounceLimit, NumSamples, GIIntensity, normalFactor, depthFactor,
	luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSRTraceParams,
	tMin, tMax, maxBounceLimit, RayMarchingMaxStep, NumSamples, Sample_Indirect_Clamp_Value,
	DistanceFactor, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSGITraceParams,
	tMin, tMax, maxBounceLimit, RayMarchingMaxStep, NumSamples, Sample_Indirect_Clamp_Value,
	GIIntensity, AOIntensity, DistanceFactor, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSAOParams, radius, bias)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::PostProcessParams, EV100, gamma)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::PostProcessFlags,
	bloomOn, gammaOn, lightDrawOn, drawTransparent, ssrOn, ssgiOn, skyboxOn,
	exponentialHeightFogOn, atmosphereOn, rayTraceReflectOn, rayTraceGIOn,
	autoExposureOn, calculateOcclusionCulling)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption,
	rayTraceGeneralParams, rayTraceReflectParams, rayTraceGIParams, ssrTraceParams,
	ssgiTraceParams, exponentialHeightFogParams, atmosphereParams, ssaoParams,
	postProcessParams, flags)

EngineRuntime::EngineRuntime() = default;

EngineRuntime::~EngineRuntime()
{
	Shutdown();
}

bool EngineRuntime::Initialize(const EngineRuntimeConfig& config)
{
	if (m_initialized)
		return false;

	m_config = config;

	if (m_config.HasModule(EngineRuntimeModule::Audio))
		m_audioDevice = std::make_shared<AudioDevice>();
	if (m_config.HasModule(EngineRuntimeModule::Input))
		m_inputContext = std::make_shared<EngineInputContext>();
	if (m_config.HasModule(EngineRuntimeModule::Render))
		m_renderer = std::make_unique<Render::Renderer>();

	if (m_renderer)
	{
		m_renderer->SetUiInputContext(m_inputContext);
		// UI 事件的出口。这个回调在渲染线程上被调用，所以它只做入队，派发交给场景线程。
		m_renderer->SetUiEventSink([this](const std::vector<UI::UiEvent>& events)
			{
				OnUiEvents(std::move(events));
			});
	}

	m_project = std::make_unique<ProjectManager>();

	RegisterEngineCommands();

	m_initialized = true;
	return true;
}

void EngineRuntime::Shutdown()
{
	if (!m_initialized)
		return;

	if (m_renderer && m_renderer->GetRenderer3D())
		m_renderer->GetRenderer3D()->StopFrameLoop();
	CloseScene();
	// 模块要在它供给的场景销毁之后再卸载。
	UnloadModules();
	if (m_renderer)
	{
		// 先摘掉 sink：外部渲染线程可能还在 WaitImage 里，不能让它回调到半销毁的运行时。
		m_renderer->SetUiEventSink(nullptr);
		if (auto* renderer3D = m_renderer->GetRenderer3D())
			renderer3D->Shutdown();
		if (auto* renderer2D = m_renderer->GetRenderer2D())
			renderer2D->Shutdown();
		m_renderer.reset();
	}
	m_project.reset();
	m_inputContext.reset();
	m_config = {};
	m_audioDevice.reset();
	m_initialized = false;
}

bool EngineRuntime::CreateRenderContext(const EngineRenderContextDesc& desc, std::string* error)
{
	// 允许先建上下文再 Initialize：建设备不依赖运行时其它服务，宿主可以在建窗口
	// 的顺序上自己安排。
	if (m_renderer && m_renderer->GetRenderer3D()->HasRenderer())
	{
		if (error)
			*error = "渲染上下文已经存在";
		return false;
	}

	// 引擎拥有实例与设备；窗口和交换链不属于引擎，这里只建设备。
	auto instance = VulkanRenderer::CreateInstance(desc.instanceExtensions);
	if (!instance)
	{
		if (error)
			*error = "创建 Vulkan 实例失败";
		return false;
	}

	std::shared_ptr<VulkanRenderer> renderer;
	if (desc.width > 0 && desc.height > 0)
		renderer = VulkanRenderer::CreateForOffScreen(instance, desc.width, desc.height);
	else
		renderer = VulkanRenderer::CreateOnlyDevice(instance, desc.deviceExtensions);

	if (!renderer)
	{
		if (error)
			*error = "创建 Vulkan 设备失败";
		return false;
	}

	BindVulkanRenderer(std::move(renderer));
	return true;
}

EngineVulkanContext EngineRuntime::GetVulkanContext() const
{
	EngineVulkanContext context;
	if (!m_renderer)
		return context;

	auto renderer = m_renderer->GetRenderer3D()->GetVulkanRenderer();
	if (!renderer)
		return context;

	auto instance = renderer->GetVulkanInstance();
	auto device = renderer->GetVulkanDevice();
	if (!instance || !device)
		return context;

	// 句柄转成不透明指针：宿主拿到后自行转回 Vulkan 类型。
	context.instance = static_cast<void*>(static_cast<VkInstance>(instance->GetHandle()));
	context.physicalDevice = static_cast<void*>(static_cast<VkPhysicalDevice>(device->GetPhysicalDevice()));
	context.device = static_cast<void*>(static_cast<VkDevice>(device->GetHandle()));
	context.graphicsQueue = static_cast<void*>(static_cast<VkQueue>(device->GetGraphicsQueue()));
	context.graphicsQueueFamily = device->GetGraphicsQueueFamily();
	context.computeQueue = static_cast<void*>(static_cast<VkQueue>(device->GetComputeQueue()));
	context.computeQueueFamily = device->GetComputeQueueFamily();
	context.descriptorPool = static_cast<void*>(static_cast<VkDescriptorPool>(VKCONTEXT->GetDescriptorPool()));
	context.valid = true;
	return context;
}

bool EngineRuntime::OpenProject(const std::string& projectFilePath, std::string* error)
{
	if (!m_initialized)
	{
		if (error)
			*error = "EngineRuntime 必须先 Initialize 才能打开工程";
		return false;
	}

	auto* project = m_project.get();
	if (project->IsProjectOpen())
		project->CloseProject();

	if (!project->OpenProject(projectFilePath))
	{
		if (error)
			*error = "打开工程失败：" + projectFilePath;
		return false;
	}
	return true;
}

void EngineRuntime::CloseProject()
{
	if (m_project)
		m_project->CloseProject();
}

bool EngineRuntime::IsProjectOpen() const
{
	auto* project = m_project.get();
	return project && project->IsProjectOpen();
}

ProjectManager* EngineRuntime::GetProjectManager() const
{
	return m_project.get();
}

Scene3DRenderOption EngineRuntime::GetSceneRenderOption() const
{
	const auto* renderer = GetRenderer3D();
	return renderer ? renderer->GetOption() : Scene3DRenderOption{};
}

void EngineRuntime::SetSceneRenderOption(const Scene3DRenderOption& option)
{
	if (auto* renderer = GetRenderer3D())
		renderer->SetOption(option);
}

void EngineRuntime::CaptureSceneRenderOption(SceneCodec& scene) const
{
	auto& settings = scene.GetSettings();
	if (!settings.is_object())
		settings = nlohmann::json::object();

	settings[kSceneRenderOptionKey] = GetSceneRenderOption();
}

void EngineRuntime::ApplySceneRenderOption(const SceneCodec& scene)
{
	Scene3DRenderOption option{};

	const auto& settings = scene.GetSettings();
	if (settings.is_object())
	{
		const auto value = settings.find(kSceneRenderOptionKey);
		if (value != settings.end() && value->is_object())
		{
			try
			{
				option = value->get<Scene3DRenderOption>();
			}
			catch (const nlohmann::json::exception&)
			{
				option = Scene3DRenderOption{};
			}
		}
	}

	SetSceneRenderOption(option);
}

bool EngineRuntime::OpenScene(const std::string& sceneAsset, const SceneOpenHooks& hooks, std::string* error)
{
	if (!m_initialized)
	{
		if (error)
			*error = "EngineRuntime 必须先 Initialize 才能打开场景";
		return false;
	}

	auto* project = m_project.get();
	if (!project->IsProjectOpen())
	{
		if (error)
			*error = "还没有打开工程，场景要靠工程里的资产库解析资源";
		return false;
	}

	auto database = project->GetDatabase();
	if (!database)
	{
		if (error)
			*error = "工程资产库不可用";
		return false;
	}

	// 一个参数两种写法：先当 GUID 找，再当工程内资源路径找。
	std::shared_ptr<AssetMeta> meta = database->GetAssetMetaByGuid(sceneAsset);
	if (!meta || meta->type != AssetType::Scene)
		meta = database->GetAssetMetaByPath(sceneAsset);

	if (!meta || meta->type != AssetType::Scene)
	{
		if (error)
			*error = "找不到场景资源：" + sceneAsset;
		return false;
	}

	auto sceneObject = project->LoadAsset(meta->guid);
	auto scene = sceneObject ? sceneObject->AsScene() : nullptr;
	if (!scene)
	{
		if (error)
			*error = "场景资源加载失败：" + sceneAsset;
		return false;
	}

	// 场景数据的反序列化与资源解析交给引擎，宿主只决定自己那部分内容。
	return OpenScene(scene, hooks, error);
}

bool EngineRuntime::OpenScene(const std::shared_ptr<SceneCodec>& scene, const SceneOpenHooks& hooks, std::string* error)
{
	if (!m_initialized)
	{
		if (error)
			*error = "EngineRuntime 必须先 Initialize 才能打开场景";
		return false;
	}

	if (!scene)
	{
		if (error)
			*error = "场景数据为空";
		return false;
	}

	auto* project = m_project.get();

	// 资源解析交给工程资产库：场景里只存 GUID，具体模型/材质/脚本由这里查出来。
	const bool opened = OpenEmptyScene([this, project, &scene, &hooks](World& world) {
		if (hooks.prepare && !hooks.prepare(world))
			return false;

		auto resolveScriptAsset = [project](const AssetGUID& guid) -> std::shared_ptr<ScriptSourceData> {
			auto database = project ? project->GetDatabase() : nullptr;
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (!meta || meta->type != AssetType::Script)
				return nullptr;
			auto asset = project->LoadAsset(guid);
			return asset ? asset->AsScript() : nullptr;
			};
		auto resolveModelAsset = [project](const AssetGUID& guid) -> std::shared_ptr<StaticModel>
			{
				if (guid == "DefaultCube")
					return RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, guid);
				if (guid == "DefaultSphere")
					return RenderHelper::GetSphereModel(0.5f, 72, 36, guid);

				auto database = project ? project->GetDatabase() : nullptr;
				auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
				if (!meta || !(meta->type == AssetType::StaticModel || meta->type == AssetType::StaticMesh))
					return nullptr;

				auto asset = project->LoadAsset(guid);
				if (!asset)
					return nullptr;

				if (meta->type == AssetType::StaticModel)
					return asset->AsStaticModel();
				if (meta->type == AssetType::StaticMesh)
				{
					auto meshes = asset->AsStaticMesh();
					if (meshes.empty())
						return nullptr;
					auto model = std::make_shared<StaticModel>();
					for (auto& mesh : meshes)
						model->AddMesh(mesh, std::make_shared<Material>());
					model->SetAssetGUID(guid);
					return model;
				}
				return nullptr;
			};
		auto resolveSkinnedModelAsset = [project](const AssetGUID& guid) -> std::shared_ptr<SkinnedModel>
			{
				auto database = project ? project->GetDatabase() : nullptr;
				auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
				if (!meta || !(meta->type == AssetType::SkinnedModel || meta->type == AssetType::SkinnedMesh))
					return nullptr;

				auto asset = project->LoadAsset(guid);
				if (!asset)
					return nullptr;

				if (meta->type == AssetType::SkinnedModel)
					return asset->AsSkinnedModel();
				if (meta->type == AssetType::SkinnedMesh)
				{
					auto meshes = asset->AsSkinnedMesh();
					if (meshes.empty())
						return nullptr;
					auto model = std::make_shared<SkinnedModel>();
					for (auto& mesh : meshes)
						model->AddMesh(mesh, std::make_shared<Material>());
					model->SetAssetGUID(guid);
					return model;
				}
				return nullptr;
			};
		auto resolveSkeletonAsset = [project](const AssetGUID& guid) -> std::shared_ptr<Skeleton> {
			auto database = project ? project->GetDatabase() : nullptr;
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (!meta || meta->type != AssetType::Skeleton)
				return nullptr;
			auto asset = project->LoadAsset(guid);
			return asset ? asset->AsSkeleton() : nullptr;
			};
		auto resolveAnimationClipAsset = [project](const AssetGUID& guid) -> std::shared_ptr<AnimationClip> {
			auto database = project ? project->GetDatabase() : nullptr;
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (!meta || meta->type != AssetType::AnimationClip)
				return nullptr;
			auto asset = project->LoadAsset(guid);
			return asset ? asset->AsAnimationClip() : nullptr;
			};
		auto resolveMaterialAsset = [project](const AssetGUID& guid) -> std::shared_ptr<Material> {
			auto database = project ? project->GetDatabase() : nullptr;
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (!meta || meta->type != AssetType::Material)
				return nullptr;
			auto asset = project->LoadAsset(guid);
			return asset ? asset->AsMaterial() : nullptr;
			};

		if (!scene->Deserialize(world, resolveScriptAsset, resolveModelAsset, resolveSkinnedModelAsset, resolveSkeletonAsset, resolveAnimationClipAsset, resolveMaterialAsset))
			return false;

		ApplySceneRenderOption(*scene);

		if (hooks.finish)
			hooks.finish(world, *scene);

		// 宿主在 prepare 里塞进来的实体和场景内容一起走一帧更新，首帧才是对的。
		world.update(16);
		return true;
		});

	if (!opened && error)
		*error = "场景反序列化失败";
	return opened;
}

void EngineRuntime::BindVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer)
{
	if (!m_renderer)
		return;
	m_renderer->SetVulkanRenderer(std::move(renderer));
	if (m_world)
		if (auto* renderSystem = m_world->getSystem<RenderSystem>())
			m_renderer->BindRenderSystem(*renderSystem);
}

bool EngineRuntime::LoadModules(const std::string& directory, std::string* error)
{
	if (!m_initialized)
	{
		if (error)
			*error = "EngineRuntime must be initialized before loading project modules";
		return false;
	}

	m_hostServices.messageBus = &m_messages;
	m_hostServices.serviceOwner = this;
	m_hostServices.requestSceneChange = &EngineRuntime::OnModuleRequestSceneChange;
	m_hostServices.requestQuit = &EngineRuntime::OnModuleRequestQuit;

	if (!m_moduleManager)
		m_moduleManager = std::make_unique<NComponentCodec::EngineModuleManager>(ToModuleHost(m_config.hostType), &m_hostServices);

	m_moduleManager->LoadModulesFromDirectory(directory);
	return true;
}

void EngineRuntime::UnloadModules()
{
	if (!m_moduleManager)
		return;

	for (const auto& moduleId : m_moduleManager->GetLoadedModuleIds())
		m_messages.UnregisterByModule(moduleId);

	m_moduleManager->UnloadAll();
	m_moduleManager.reset();
}

void EngineRuntime::StepEditorFrame(float deltaMilliseconds)
{
	if (!m_world || m_world->isRunning())
		return;

	m_messages.Discard();

	m_world->ProcessCommands();

	if (auto* physicsSystem = m_world->getSystem<PhysicsSystem>())
		physicsSystem->fixedUpdate(0.f);

	if (m_inputContext)
	{
		const auto inputFrame = m_inputContext->ConsumeFrame();
		for (Entity entity : m_world->getAllEntity())
		{
			if (!entity.hasComponents<TagCurrentControl, TagFreeCamera, PlayerInput, Controller>())
				continue;

			auto& input = entity.getComponent<PlayerInput>();
			auto& controller = entity.getComponent<Controller>();

			SyncPlayerInput(input, inputFrame);
			ApplyDefaultControllerInput(input, controller);
		}
	}

	if (auto* cameraSystem = m_world->getSystem<CameraFollowSystem>())
		cameraSystem->postUpdate(deltaMilliseconds);
	if (auto* animationSystem = m_world->getSystem<AnimationSystem>())
		animationSystem->postUpdate(0.f);
	if (auto* renderSystem = m_world->getSystem<RenderSystem>())
		renderSystem->postUpdate(deltaMilliseconds);
}

void EngineRuntime::StartSceneThread(float logicDeltaMilliseconds, float fixedDeltaMilliseconds)
{
	if (!m_world || m_sceneThread || m_world->isRunning())
		return;

	m_world->setLogicDeltaTime(logicDeltaMilliseconds);
	m_world->setFixedDeltaTime(fixedDeltaMilliseconds);
	m_world->start();

	// 记下来：切完场景要按同样的节拍把线程重新拉起来。
	m_logicDeltaMilliseconds = logicDeltaMilliseconds;
	m_fixedDeltaMilliseconds = fixedDeltaMilliseconds;

	m_sceneStop = false;
	m_sceneThread = std::make_shared<std::thread>(&EngineRuntime::SceneThreadLoop, this);
}

void EngineRuntime::SceneThreadLoop()
{
	auto world = m_world;
	if (!world)
		return;

	const float logicDelta = world->getLogicDeltaTime();
	const float fixedDelta = world->getFixedDeltaTime();
	const uint32_t targetFps = static_cast<uint32_t>(1000.f / std::min(logicDelta, fixedDelta)) + 1u;

	DynamicFpsController fpsController(targetFps);
	fpsController.reset();
	while (!m_sceneStop)
	{
		PumpUiEvents();
		world->update(fpsController.getTimeDiffMS());
		fpsController.run();
	}
}

void EngineRuntime::StopSceneThread()
{
	m_sceneStop = true;
	if (m_sceneThread)
	{
		if (m_sceneThread->joinable())
			m_sceneThread->join();
		m_sceneThread.reset();
	}
	// 场景线程停掉之后 World 才不再接受 update：先 join 再 stop，避免线程还在跑时被关。
	if (m_world)
		m_world->stop();
}

void EngineRuntime::SetTargetFps(uint32_t fps)
{
	if (m_renderer)
		m_renderer->GetRenderer3D()->SetTargetFps(fps);
}

uint32_t EngineRuntime::GetTargetFps() const
{
	return m_renderer ? m_renderer->GetRenderer3D()->GetTargetFps() : 0u;
}

void EngineRuntime::SetWorldTimeScale(float scale)
{
	if (!std::isfinite(scale))
		return;

	m_worldTimeScale = std::clamp(scale, 0.0f, 20.0f);

	if (m_world)
		m_world->setTimeScale(m_worldTimeScale);
}

float EngineRuntime::GetWorldTimeScale() const
{
	// 以当前 World 为准：宿主可能绕过引擎直接改过它。
	return m_world ? m_world->getTimeScale() : m_worldTimeScale;
}

Render::Renderer* EngineRuntime::GetRenderer()
{
	return m_renderer.get();
}

const Render::Renderer* EngineRuntime::GetRenderer() const
{
	return m_renderer.get();
}

Render::Renderer3D* EngineRuntime::GetRenderer3D()
{
	if (!m_renderer || !m_config.HasModule(EngineRuntimeModule::Render))
		return nullptr;
	return m_renderer->GetRenderer3D();
}

const Render::Renderer3D* EngineRuntime::GetRenderer3D() const
{
	if (!m_renderer || !m_config.HasModule(EngineRuntimeModule::Render))
		return nullptr;
	return m_renderer->GetRenderer3D();
}

bool EngineRuntime::CreateRenderer2D(ID3D11Device* device, IDXGISwapChain1* swapChain, std::string* error)
{
	if (!device || !swapChain)
	{
		if (error)
			*error = "创建 2D 服务需要宿主的 D3D11 设备与交换链";
		return false;
	}

	if (!m_renderer)
		return false;

	return m_renderer->GetRenderer2D()->Initialize(device, swapChain, error);
}

Render::Renderer2D* EngineRuntime::GetRenderer2D()
{
	return m_renderer ? m_renderer->GetRenderer2D() : nullptr;
}

const Render::Renderer2D* EngineRuntime::GetRenderer2D() const
{
	return m_renderer ? m_renderer->GetRenderer2D() : nullptr;
}

void EngineRuntime::RegisterBaseSystems(World& world)
{
	if (!m_initialized)
		throw std::logic_error("EngineRuntime must be initialized before opening a scene");

	// 先注册完整的内置运行时流水线，之后才允许项目模块注册它自己的系统。
	world.registerSystem<ScriptSystem>(ToSystemPriority(EngineSystemPriority::Script));
	// 输入/音频系统跟着对应服务走：服务没建就不注册，免得系统拿到空服务。
	if (m_inputContext)
		world.registerSystem<LocalInputSystem>(ToSystemPriority(EngineSystemPriority::Input), m_inputContext);
	world.registerSystem<LifetimeSystem>(ToSystemPriority(EngineSystemPriority::Lifetime));
	world.registerSystem<PhysicsSystem>(ToSystemPriority(EngineSystemPriority::Physics));
	world.registerSystem<DestroySystem>(ToSystemPriority(EngineSystemPriority::Destroy));
	world.registerSystem<CameraFollowSystem>(ToSystemPriority(EngineSystemPriority::CameraFollow));
	if (m_audioDevice)
		world.registerSystem<AudioSystem>(ToSystemPriority(EngineSystemPriority::Audio), m_audioDevice);
	world.registerSystem<AnimationSystem>(ToSystemPriority(EngineSystemPriority::Animation));
	world.registerSystem<ParticleSystem>(ToSystemPriority(EngineSystemPriority::Particle));
	world.registerSystem<LaserBeamSystem>(ToSystemPriority(EngineSystemPriority::LaserBeam));

	world.registerSystem<UIViewSystem>(ToSystemPriority(EngineSystemPriority::Render) + 100);
	if (auto* uiViewSystem = world.getSystem<UIViewSystem>())
	{
		uiViewSystem->SetDocumentLoader([this](const std::string& guid) -> std::shared_ptr<UI::ParseResult>
			{
				if (!m_project || guid.empty())
					return nullptr;

				auto asset = m_project->LoadAsset(guid);
				if (!asset)
					return nullptr;

				return asset->AsUIDocument();
			});
	}

	m_renderer->RegisterRenderSystem(world);
}

bool EngineRuntime::OpenEmptyScene(const std::function<bool(World&)>& initializeScene)
{
	if (!m_initialized)
		return false;

	auto candidate = std::make_shared<World>();
	candidate->setTimeScale(m_worldTimeScale);
	RegisterBaseSystems(*candidate);
	if (m_moduleManager)
		m_moduleManager->RegisterSystems(*candidate);
	if (initializeScene && !initializeScene(*candidate))
		return false;

	CloseScene();
	m_world = std::move(candidate);
	return true;
}

void EngineRuntime::CloseScene()
{
	StopSceneThread();
	if (m_world && m_world->isRunning())
		m_world->stop();
	if (m_world)
		m_world->ProcessCommands();
	m_world.reset();
}

void EngineRuntime::OnUiEvents(const std::vector<UI::UiEvent>& events)
{
	if (!events.empty())
		m_messages.Post(std::move(events));
}

void EngineRuntime::PumpUiEvents()
{
	if (!m_world)
		return;

	m_lastPumpReport = m_messages.Pump(*m_world);
}

void EngineRuntime::RequestSceneChange(const std::string& sceneAsset)
{
	if (sceneAsset.empty())
		return;

	{
		std::lock_guard<std::mutex> lock(m_pendingSceneMutex);
		m_pendingSceneAsset = sceneAsset;
	}
	m_hasPendingScene = true;
}

bool EngineRuntime::TakeSceneChangeRequest(std::string& sceneAsset)
{
	if (!m_hasPendingScene)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_pendingSceneMutex);
		sceneAsset = std::move(m_pendingSceneAsset);
		m_pendingSceneAsset.clear();
	}

	m_hasPendingScene = false;

	return !sceneAsset.empty();
}

bool EngineRuntime::PumpSceneChanges(std::string* error)
{
	std::string sceneAsset;
	if (!TakeSceneChangeRequest(sceneAsset))
		return false;

	const bool wasThreaded = static_cast<bool>(m_sceneThread);
	StopSceneThread();

	const bool opened = OpenScene(sceneAsset, {}, error);
	if (wasThreaded && opened)
		StartSceneThread(m_logicDeltaMilliseconds, m_fixedDeltaMilliseconds);
	return opened;
}

void EngineRuntime::OnModuleRequestSceneChange(void* owner, const char* sceneAsset)
{
	auto* runtime = static_cast<EngineRuntime*>(owner);
	if (!runtime || !sceneAsset)
		return;
	runtime->RequestSceneChange(sceneAsset);
}

void EngineRuntime::RequestQuit()
{
	m_hasQuitRequest = true;
}

bool EngineRuntime::TakeQuitRequest()
{
	return m_hasQuitRequest.exchange(false);
}

void EngineRuntime::OnModuleRequestQuit(void* owner)
{
	auto* runtime = static_cast<EngineRuntime*>(owner);
	if (!runtime)
		return;
	runtime->RequestQuit();
}

void EngineRuntime::RegisterEngineCommands()
{
	UI::MessageHandler handler = [this](const UI::Message& message, World& world)
		{
			ApplyEngineCommand(message, world);
		};

	for (const auto& desc : UI::EngineCommands::Table)
		m_messages.Register(UI::EngineModuleId, desc.name, handler);
}

void EngineRuntime::ApplyEngineCommand(const UI::Message& message, World& world)
{
	if (message.topic == UI::EngineCommands::TargetFps)
	{
		if (!message.Find("value"))
			return;

		const int fps = std::clamp(static_cast<int>(message.GetInteger("value")), 0, 500);
		SetTargetFps(static_cast<uint32_t>(fps));
	}
	else if (message.topic == UI::EngineCommands::MasterVolume)
	{
		if (m_audioDevice && message.Find("value"))
			m_audioDevice->SetMasterVolumn(std::clamp(static_cast<float>(message.GetInteger("value")), 0.0f, 100.0f) / 100.0f);
	}
	else if (message.topic == UI::EngineCommands::ChannelVolume)
	{
		if (m_audioDevice && message.Find("channel") && message.Find("value"))
		{
			const AudioChannelID channel = message.GetUint32("channel");
			m_audioDevice->SetChannelVolumn(channel, std::clamp(static_cast<float>(message.GetInteger("value")), 0.0f, 100.0f) / 100.0f);
		}
	}
}
