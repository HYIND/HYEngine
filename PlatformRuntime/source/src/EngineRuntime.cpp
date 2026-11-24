#include "EngineRuntime.h"
#include "Audio/Systems/AudioSystem.h"
#include "Input/EngineInputContext.h"
#include "Input/Systems/LocalInputSystem.h"
#include "Input/Components/PlayerInput.h"
#include "Input/ControllerInput.h"
#include "Components/Controller.h"
#include "Components/CameraComponent.h"
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
#include "Render/Systems/Render2DResourceSystem.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "UI/EngineCommands.h"
#include "UI/UiTypes.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cwctype>
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

	void ReadOptionUVec3(const nlohmann::json& json, const char* key, glm::uvec3& out)
	{
		const auto value = json.find(key);
		if (value == json.end() || !value->is_array() || value->size() < 3)
			return;
		for (nlohmann::json::size_type index = 0; index < 3; ++index)
		{
			const auto& component = (*value)[index];
			if (component.is_number_unsigned() || component.is_number_integer())
				out[static_cast<int>(index)] = component.get<uint32_t>();
		}
	}
}

// DDGI 参数。旧场景里的体积字段（autoFitVolume / followCamera / center / counts / spacing 等）已废弃，读取时直接忽略。
void to_json(nlohmann::json& json, const Scene3DRenderOption::DDGIParams& params)
{
	json = nlohmann::json{
		{"cascadeEnable", params.cascadeEnable},
		{"cascadeLevels", params.cascadeLevels},
		{"cascadeBaseSpacing", {params.cascadeBaseSpacing.x, params.cascadeBaseSpacing.y, params.cascadeBaseSpacing.z}},
		{"cascadeCounts", {params.cascadeCounts.x, params.cascadeCounts.y, params.cascadeCounts.z}},
		{"cascadeFadeCells", params.cascadeFadeCells},
		{"cascadeAutoRelocate", params.cascadeAutoRelocate},
		{"normalBias", params.normalBias},
		{"viewBias", params.viewBias},
		{"maxRayDistance", params.maxRayDistance},
		{"backfaceThreshold", params.backfaceThreshold},
		{"skyIntensity", params.skyIntensity},
		{"energy", params.energy},
		{"updateBudget", params.updateBudget},
		{"hysteresis", params.hysteresis},
		{"lightChangeThreshold", params.lightChangeThreshold}
	};
}

void from_json(const nlohmann::json& json, Scene3DRenderOption::DDGIParams& params)
{
	ReadOptionField(json, "cascadeEnable", params.cascadeEnable);
	ReadOptionField(json, "cascadeLevels", params.cascadeLevels);
	ReadOptionVec3(json, "cascadeBaseSpacing", params.cascadeBaseSpacing);
	ReadOptionUVec3(json, "cascadeCounts", params.cascadeCounts);
	ReadOptionField(json, "cascadeFadeCells", params.cascadeFadeCells);
	ReadOptionField(json, "cascadeAutoRelocate", params.cascadeAutoRelocate);
	ReadOptionField(json, "normalBias", params.normalBias);
	ReadOptionField(json, "viewBias", params.viewBias);
	ReadOptionField(json, "maxRayDistance", params.maxRayDistance);
	ReadOptionField(json, "backfaceThreshold", params.backfaceThreshold);
	ReadOptionField(json, "skyIntensity", params.skyIntensity);
	ReadOptionField(json, "energy", params.energy);
	ReadOptionField(json, "updateBudget", params.updateBudget);
	ReadOptionField(json, "hysteresis", params.hysteresis);
	ReadOptionField(json, "lightChangeThreshold", params.lightChangeThreshold);
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

void to_json(nlohmann::json& json, const Scene3DRenderOption::VolumetricCloudParams& params)
{
	json = nlohmann::json{
		{"cloudAlbedo", {params.cloudAlbedo.x, params.cloudAlbedo.y, params.cloudAlbedo.z}},
		{"cloudBottomHeight", params.cloudBottomHeight},
		{"cloudAmbientColor", {params.cloudAmbientColor.x, params.cloudAmbientColor.y, params.cloudAmbientColor.z}},
		{"cloudTopHeight", params.cloudTopHeight},
		{"coverage", params.coverage},
		{"density", params.density},
		{"shapeScale", params.shapeScale},
		{"detailScale", params.detailScale},
		{"detailStrength", params.detailStrength},
		{"phaseG", params.phaseG},
		{"phaseBlend", params.phaseBlend},
		{"powder", params.powder},
		{"maxTraceDistance", params.maxTraceDistance},
		{"erosionDepth", params.erosionDepth},
		{"viewStepCount", params.viewStepCount},
		{"lightStepCount", params.lightStepCount}
	};
}

void from_json(const nlohmann::json& json, Scene3DRenderOption::VolumetricCloudParams& params)
{
	ReadOptionVec3(json, "cloudAlbedo", params.cloudAlbedo);
	ReadOptionField(json, "cloudBottomHeight", params.cloudBottomHeight);
	ReadOptionVec3(json, "cloudAmbientColor", params.cloudAmbientColor);
	ReadOptionField(json, "cloudTopHeight", params.cloudTopHeight);
	ReadOptionField(json, "coverage", params.coverage);
	ReadOptionField(json, "density", params.density);
	ReadOptionField(json, "shapeScale", params.shapeScale);
	ReadOptionField(json, "detailScale", params.detailScale);
	ReadOptionField(json, "detailStrength", params.detailStrength);
	ReadOptionField(json, "phaseG", params.phaseG);
	ReadOptionField(json, "phaseBlend", params.phaseBlend);
	ReadOptionField(json, "powder", params.powder);
	ReadOptionField(json, "maxTraceDistance", params.maxTraceDistance);
	ReadOptionField(json, "erosionDepth", params.erosionDepth);
	ReadOptionField(json, "viewStepCount", params.viewStepCount);
	ReadOptionField(json, "lightStepCount", params.lightStepCount);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceGeneralParams,
	maxDistance, maxCacheClearDistance)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceReflectParams,
	tMin, tMax, maxBounceLimit, NumSamples, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale, varianceClipGamma,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::RayTraceGIParams,
	tMin, tMax, maxBounceLimit, NumSamples, GIIntensity, normalFactor, depthFactor,
	luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale, varianceClipGamma,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSRTraceParams,
	tMin, tMax, maxBounceLimit, RayMarchingMaxStep, NumSamples,
	DistanceFactor, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale, varianceClipGamma,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSGITraceParams,
	tMin, tMax, maxBounceLimit, RayMarchingMaxStep, NumSamples,
	GIIntensity, AOIntensity, DistanceFactor, normalFactor, depthFactor, luminanceFactor,
	depthThreshold, positiondistThreshold, normalThreshold, motionWeightScale, varianceClipGamma,
	maxAccumulateCount, filterCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::SSAOParams, radius, bias)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::PostProcessParams, EV100, gamma)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::TransparentParams, mode)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption::PostProcessFlags,
	bloomOn, gammaOn, lightDrawOn, ddgiProbeDrawOn, drawTransparent, ssrOn, ssgiOn, skyboxOn,
	exponentialHeightFogOn, atmosphereOn, volumetricCloudOn, rayTraceReflectOn, rayTraceGIOn,
	ddgiOn, autoExposureOn, calculateOcclusionCulling)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene3DRenderOption,
	rayTraceGeneralParams, rayTraceReflectParams, rayTraceGIParams, ssrTraceParams,
	ssgiTraceParams, exponentialHeightFogParams, atmosphereParams, volumetricCloudParams,
	ssaoParams, postProcessParams, transparentParams, ddgiParams, flags)

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

namespace
{
	// settings.editorFreeCamera：编辑器保存场景时记下的视角。读出可用的相机位姿返回 true；
	// fov/near/far 单独校验，合法才覆盖。
	bool ReadSavedViewCamera(const nlohmann::json* settings, Transform& transform, CameraComponent& camera)
	{
		if (!settings || !settings->is_object())
			return false;
		const auto it = settings->find("editorFreeCamera");
		if (it == settings->end() || !it->is_object())
			return false;
		const auto& data = *it;

		const auto readFloats = [](const nlohmann::json& value, size_t count, float* out) {
			if (!value.is_array() || value.size() != count)
				return false;
			for (size_t i = 0; i < count; ++i)
			{
				if (!value[i].is_number())
					return false;
				out[i] = value[i].get<float>();
				if (!std::isfinite(out[i]))
					return false;
			}
			return true;
			};
		const auto readFloat = [](const nlohmann::json& object, const char* key, float& out) {
			const auto value = object.find(key);
			if (value == object.end() || !value->is_number())
				return false;
			out = value->get<float>();
			return std::isfinite(out);
			};

		try
		{
			float fov = 0.f, nearPlane = 0.f, farPlane = 0.f;
			if (readFloat(data, "fov", fov) && readFloat(data, "nearPlane", nearPlane) && readFloat(data, "farPlane", farPlane)
				&& fov > 0.f && fov < 180.f && nearPlane > 0.f && farPlane > nearPlane)
			{
				camera.fov = fov;
				camera.nearPlane = nearPlane;
				camera.farPlane = farPlane;
			}

			const auto cameraTransform = data.find("cameraTransform");
			if (cameraTransform == data.end() || !cameraTransform->is_object()
				|| !cameraTransform->contains("position") || !cameraTransform->contains("rotation"))
				return false;

			float position[3];
			float rotation[4];
			if (!readFloats(cameraTransform->at("position"), 3, position) || !readFloats(cameraTransform->at("rotation"), 4, rotation))
				return false;

			glm::quat quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);	// 存的顺序是 w, x, y, z
			const float length = glm::length(quaternion);
			if (!std::isfinite(length) || length <= 1e-6f)
				return false;

			transform.position = glm::vec3(position[0], position[1], position[2]);
			transform.rotation = glm::normalize(quaternion);
			return true;
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	// 静止的兜底相机：位姿优先取场景里存的编辑器视角，否则用和编辑器默认一样的视角（(0,10,10)，yaw -90，pitch -45）。
	Entity CreateFallbackCamera(World& world, const nlohmann::json* settings)
	{
		Transform transform(glm::vec3(0.f, 10.f, 10.f));
		CameraComponent camera;
		camera.fov = 80.f;
		camera.nearPlane = 0.1f;
		camera.farPlane = 800.f;

		if (!ReadSavedViewCamera(settings, transform, camera))
		{
			// 与 CameraFollowSystem 的约定一致：Transform::GetDirection() = rotation * (0,0,1) = 相机朝向
			CameraFrontData front(-90.f, -45.f, camera.worldUp);
			transform.rotation = glm::quatLookAt(-glm::normalize(front.cameraFront), glm::vec3(0.f, 1.f, 0.f));
		}

		Entity entity = world.createEntity();
		entity.addComponent<Transform>() = transform;
		auto& cameraComponent = entity.addComponent<CameraComponent>();
		cameraComponent.worldUp = camera.worldUp;
		cameraComponent.fov = camera.fov;
		cameraComponent.nearPlane = camera.nearPlane;
		cameraComponent.farPlane = camera.farPlane;
		return entity;
	}

	// 宿主已经设了主相机（编辑器的自由相机）就不动；否则选场景相机，再没有就建兜底相机。
	// 走 SetMainCamera，World 的主相机记录和 TagMainCamera 才一致（Audio 等按 GetMainCameraEntity 取）。
	void EnsureMainCamera(World& world, const nlohmann::json* settings)
	{
		if (world.HasMainCameraEntity())
			return;

		Entity camera = EngineRuntime::FindSceneMainCamera(world);
		if (!camera)
			camera = CreateFallbackCamera(world, settings);
		world.SetMainCamera(camera);
	}
}

Entity EngineRuntime::FindSceneMainCamera(World& world, const std::function<bool(Entity)>& exclude)
{
	Entity firstCamera;
	for (Entity entity : world.getEntitiesWith<Transform, CameraComponent>())
	{
		if (exclude && exclude(entity))
			continue;
		if (entity.hasComponent<TagMainCamera>())
			return entity;
		if (!firstCamera)
			firstCamera = entity;
	}
	return firstCamera;
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

		// 宿主没给相机就用场景的；场景里也没有就按场景存的视角建兜底相机（否则 3D 帧没有相机，视图矩阵全是 NaN）
		EnsureMainCamera(world, &scene->GetSettings());

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
	// 编辑态不推进动图时间，但要解析资源：拖进来的精灵 / 动图马上就能看到（停在当前帧）
	if (auto* resource2D = m_world->getSystem<Render2DResourceSystem>())
		resource2D->postUpdate(0.f);
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

std::string EngineRuntime::FindImageFile(const std::string& path) const
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path reference(reinterpret_cast<const char8_t*>(path.c_str()));

	auto existing = [&](const fs::path& candidate) -> std::string
		{
			if (candidate.empty() || !fs::is_regular_file(candidate, ec))
				return {};
			const auto canonical = fs::weakly_canonical(candidate, ec);
			const auto u8 = (ec ? candidate : canonical).u8string();
			return std::string(u8.begin(), u8.end());
		};

	// 只在工程目录内解析：工程不能依赖工程外的文件（不再找工作目录和它的上级目录），
	// 写成绝对路径的也必须落在工程目录里
	if (!m_project || !m_project->IsProjectOpen())
		return {};

	const auto projectPath = m_project->GetProjectFolderFullPath();
	const fs::path projectRoot = fs::weakly_canonical(fs::path(reinterpret_cast<const char8_t*>(projectPath.c_str())), ec);
	const std::string found = existing(reference.is_absolute() ? reference : projectRoot / reference);
	if (found.empty())
		return {};

	auto lower = [](std::wstring s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return s;
		};
	std::wstring root = lower(projectRoot.generic_wstring());
	if (!root.empty() && root.back() != L'/')
		root += L'/';
	const std::wstring resolved = lower(fs::path(reinterpret_cast<const char8_t*>(found.c_str())).generic_wstring());
	if (root.size() <= 1 || resolved.compare(0, root.size(), root) != 0)
		return {};
	return found;
}

void EngineRuntime::ClearImageFileCache()
{
	std::lock_guard<std::mutex> lock(m_imageFileCacheMutex);
	m_imageFileCache.clear();
	m_imageFileMissing.clear();
}

Render2D::ImageSource EngineRuntime::ResolveImageReference(const std::string& reference)
{
	std::string assetPart, region;
	Render2D::SplitImageReference(reference, assetPart, region);
	if (assetPart.empty())
		return {};

	// 1. 资产 GUID
	if (m_project && m_project->IsProjectOpen())
	{
		auto database = m_project->GetDatabase();
		if (auto meta = database ? database->GetAssetMetaByGuid(assetPart) : nullptr)
		{
			auto asset = m_project->LoadAsset(meta->guid);
			if (!asset)
				return {};

			Render2D::ImageSource source;
			source.region = region;
			if (meta->type == AssetType::Sprite || meta->type == AssetType::Texture)
				source.sprite = asset->AsSprite();
			else if (meta->type == AssetType::AnimatedImage)
				source.animated = asset->AsAnimatedImage();
			return source;
		}
	}

	// 2. 图片文件路径（UI 文档里常见的写法）
	const std::string projectFolder = (m_project && m_project->IsProjectOpen()) ? m_project->GetProjectFolderFullPath() : std::string();
	{
		std::lock_guard<std::mutex> lock(m_imageFileCacheMutex);
		if (m_imageFileCacheProject != projectFolder)
		{
			m_imageFileCache.clear();
			m_imageFileMissing.clear();
			m_imageFileCacheProject = projectFolder;
		}
		if (m_imageFileMissing.contains(assetPart))
			return {};
		if (auto it = m_imageFileCache.find(assetPart); it != m_imageFileCache.end())
		{
			auto source = it->second;
			source.region = region;
			return source;
		}
	}

	Render2D::ImageSource source;
	const std::string file = FindImageFile(assetPart);
	if (!file.empty())
	{
		// 直接加载的图片用线性过滤、不做 sRGB 转换（和 UI 的颜色空间一致）
		TextureConfig config;
		config.minFilter = vk::Filter::eLinear;
		config.magFilter = vk::Filter::eLinear;

		std::string extension = std::filesystem::path(reinterpret_cast<const char8_t*>(file.c_str())).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (extension == ".gif")
			source.animated = Render2D::AnimatedImage::LoadFromFile(file, config);
		else if (auto texture = std::make_shared<Texture2D>(file, config, false); texture && !texture->IsEmpty())
			source.sprite = std::make_shared<Render2D::Sprite>(std::move(texture), std::vector<Render2D::SpriteRegion>{});
	}

	std::lock_guard<std::mutex> lock(m_imageFileCacheMutex);
	if (!source.IsValid())
	{
		m_imageFileMissing[assetPart] = true;
		return {};
	}
	m_imageFileCache[assetPart] = source;
	source.region = region;
	return source;
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

	// 2D 资源桥：比 UIViewSystem 晚（文档先加载），比 RenderSystem 早（收集前资源已解析好）
	auto& resource2D = world.registerSystem<Render2DResourceSystem>(ToSystemPriority(EngineSystemPriority::Render) + 50);
	resource2D.SetImageResolver([this](const std::string& reference) { return ResolveImageReference(reference); });

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

	// 完全空的场景（比如工程没设默认场景）也给一台兜底相机；OpenScene 走到这里时已经有主相机了，不会重复建
	EnsureMainCamera(*candidate, nullptr);

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
