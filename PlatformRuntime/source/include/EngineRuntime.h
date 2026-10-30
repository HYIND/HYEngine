#pragma once

#include "ECSCore/World.h"
#include "SceneCodec/ComponentCodec.h"
#include "SceneCodec/ModuleHostServices.h"
#include "UI/MessageBus.h"

#include "Project/ProjectManager.h"

#include <glm/glm.hpp>
#include "VulkanRenderEngine/RenderOption.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioDevice;
class EngineInputContext;
class SceneCodec;
namespace Render { class Renderer; class Renderer3D; class Renderer2D; }
struct ID3D11Device;
struct IDXGISwapChain1;
class VulkanRenderer;

enum class EngineHostType : uint32_t
{
	Editor,
	Game,
	Server,
};

enum class EngineRuntimeModule : uint32_t
{
	Audio,       // 音频设备与 AudioSystem
	Input,       // 输入上下文与 LocalInputSystem
	Render,      // 渲染服务：RenderUI、Render2D和Render3D
};

struct EngineRuntimeConfig
{
	EngineHostType hostType;

	std::vector<EngineRuntimeModule> modules = {
		EngineRuntimeModule::Audio,
		EngineRuntimeModule::Input,
		EngineRuntimeModule::Render,
	};

	bool HasModule(EngineRuntimeModule module) const
	{
		return std::find(modules.begin(), modules.end(), module) != modules.end();
	}
};

struct EngineRenderContextDesc
{
	std::vector<std::string> instanceExtensions;
	std::vector<std::string> deviceExtensions;

	// 离屏目标尺寸。填了就在建上下文时一并建离屏目标；
	// 留 0 表示只建设备，目标由宿主之后自己按窗口尺寸创建。
	uint32_t width = 0;
	uint32_t height = 0;
};

// 交给宿主的 Vulkan 上下文句柄：引擎拥有实例/设备/队列，宿主拿句柄去接自己的
// UI 后端或合成路径。句柄用不透明指针表达，这个头文件因此不依赖 Vulkan 类型，
// 宿主需要时自行转成 VkInstance / VkDevice 等。
struct EngineVulkanContext
{
	void* instance = nullptr;
	void* physicalDevice = nullptr;
	void* device = nullptr;
	void* graphicsQueue = nullptr;
	uint32_t graphicsQueueFamily = 0;
	void* computeQueue = nullptr;
	uint32_t computeQueueFamily = 0;
	void* descriptorPool = nullptr;
	bool valid = false;
};

class EngineRuntime
{
public:
	EngineRuntime();
	~EngineRuntime();
	EngineRuntime(const EngineRuntime&) = delete;
	EngineRuntime& operator=(const EngineRuntime&) = delete;

	bool Initialize(const EngineRuntimeConfig& config = {});
	void Shutdown();
	bool IsInitialized() const noexcept { return m_initialized; }


	bool CreateRenderContext(const EngineRenderContextDesc& desc, std::string* error = nullptr);
	EngineVulkanContext GetVulkanContext() const;	// 当前上下文句柄。引擎还没有上下文时 valid 为 false。

	Render::Renderer* GetRenderer();
	const Render::Renderer* GetRenderer() const;

	// 启动列表里没有 Renderer3D 时返回 nullptr，调用方需要自己判空。
	Render::Renderer3D* GetRenderer3D();
	const Render::Renderer3D* GetRenderer3D() const;

	// 2D 能力：D3D11 设备与交换链由宿主创建并持有，引擎只借用句柄建 D2D 设备上下文。
	bool CreateRenderer2D(ID3D11Device* device, IDXGISwapChain1* swapChain, std::string* error = nullptr);
	Render::Renderer2D* GetRenderer2D();
	const Render::Renderer2D* GetRenderer2D() const;

	std::shared_ptr<AudioDevice> GetAudioDevice() const { return m_audioDevice; }

	// 场景正在引擎的场景线程上推进时为真（播放态）。
	bool IsSceneOpen() const noexcept { return m_world != nullptr; }
	bool IsSceneThreaded() const noexcept { return m_world && m_world->isRunning(); }

	void StartSceneThread(float logicDeltaMilliseconds, float fixedDeltaMilliseconds);
	void StopSceneThread();
	void StepEditorFrame(float deltaMilliseconds);

	// 3D 帧消费者的目标帧率（0 表示不限帧）
	void SetTargetFps(uint32_t fps);
	uint32_t GetTargetFps() const;

	void SetWorldTimeScale(float scale);
	float GetWorldTimeScale() const;

	bool LoadModules(const std::string& directory, std::string* error = nullptr);

	bool OpenProject(const std::string& projectFilePath, std::string* error = nullptr);
	void CloseProject();
	bool IsProjectOpen() const;
	ProjectManager* GetProjectManager() const;

	Scene3DRenderOption GetSceneRenderOption() const;
	void SetSceneRenderOption(const Scene3DRenderOption& option);

	void CaptureSceneRenderOption(SceneCodec& scene) const;
	// 以场景为准：文件里没记录就还原成引擎默认值，不沿用渲染器当前值。
	void ApplySceneRenderOption(const SceneCodec& scene);

	bool OpenEmptyScene(const std::function<bool(World&)>& initializeScene);

	struct SceneOpenHooks
	{
		std::function<bool(World&)> prepare;
		std::function<void(World&, const SceneCodec&)> finish;
	};

	// sceneAsset 既可以是场景资源的 GUID，也可以是工程内的资源路径。
	bool OpenScene(const std::string& sceneAsset, const SceneOpenHooks& hooks = {}, std::string* error = nullptr);
	bool OpenScene(const std::shared_ptr<SceneCodec>& scene, const SceneOpenHooks& hooks = {}, std::string* error = nullptr);

	void CloseScene();
	World* GetWorld() const noexcept { return m_world.get(); }

	void RequestSceneChange(const std::string& sceneAsset);
	bool HasPendingSceneChange() const noexcept { return m_hasPendingScene; }
	bool PumpSceneChanges(std::string* error = nullptr);	// 有登记就切一次、并按原节拍重启场景线程；返回是否真的切了。宿主每帧调一次。
	bool TakeSceneChangeRequest(std::string& sceneAsset);	// 只取走登记、不执行：宿主想走自己的打开流程时用它（编辑器要维护编辑相机、选中、播放快照）。

	void RequestQuit();
	bool TakeQuitRequest();

	UI::MessageBus& GetMessageBus() noexcept { return m_messages; }
	const UI::PumpReport& GetLastUiPumpReport() const noexcept { return m_lastPumpReport; }

	std::shared_ptr<EngineInputContext> GetInputContext() const noexcept { return m_inputContext; }

private:
	void RegisterBaseSystems(World& world);
	void BindVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer);
	void SceneThreadLoop();

	void UnloadModules(); // 只在 Shutdown 里用：模块必须在它们供给的场景销毁之后才卸载。

	void OnUiEvents(const std::vector<UI::UiEvent>& events);  // 渲染线程的 sink 落点：只入队，不派发。
	void PumpUiEvents();    // 场景线程每帧调：把队列里的 UI 事件派发给模块的 handler。

	void RegisterEngineCommands();
	void ApplyEngineCommand(const UI::Message& message, World& world);

	static void OnModuleRequestSceneChange(void* owner, const char* sceneAsset); // 模块经 ModuleHostServices 回调进来的切场景入口。
	static void OnModuleRequestQuit(void* owner);   // 模块经 ModuleHostServices 回调进来的退出入口。

private:

	EngineRuntimeConfig m_config;
	std::shared_ptr<World> m_world;
	std::shared_ptr<std::thread> m_sceneThread;
	std::atomic<bool> m_sceneStop{ true };
	std::unique_ptr<ProjectManager> m_project;
	std::shared_ptr<EngineInputContext> m_inputContext;
	std::shared_ptr<AudioDevice> m_audioDevice;
	std::unique_ptr<Render::Renderer> m_renderer;
	std::unique_ptr<NComponentCodec::EngineModuleManager> m_moduleManager;
	bool m_initialized = false;

	// UI 事件总线不随场景重建：模块的订阅不会因为换场景而丢。
	UI::MessageBus m_messages;
	UI::PumpReport m_lastPumpReport;

	// 交给模块的宿主服务
	NComponentCodec::ModuleHostServices m_hostServices;

	// 待执行的场景切换。登记和执行分开，中间只隔一个帧边界。
	std::mutex m_pendingSceneMutex;
	std::string m_pendingSceneAsset;
	std::atomic<bool> m_hasPendingScene{ false };

	// 待执行的退出请求
	std::atomic<bool> m_hasQuitRequest{ false };

	// 启动场景线程时用的步长，切完场景要按同样节拍重启。
	float m_logicDeltaMilliseconds = 16.0f;
	float m_fixedDeltaMilliseconds = 16.0f;
	// 世界时间膨胀系数。切场景时新建的 World 也要继承它，否则换场景会悄悄回到 1x。
	float m_worldTimeScale = 1.0f;
};
