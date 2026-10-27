#pragma once

#include "ECSCore/World.h"
#include "SceneCodec/ComponentCodec.h"
#include "SceneCodec/ModuleHostServices.h"
#include "UI/UICommandBus.h"

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
class ProjectManager;
class SceneCodec;
namespace Render { class Renderer; class Renderer3D; class Renderer2D; }
struct ID3D11Device;
struct IDXGISwapChain1;
class VulkanRenderer;

// 当前宿主类型。用引擎自己的枚举表达，宿主因此不需要知道模块 ABI 里的类型。
enum class EngineHostKind : uint32_t
{
    Editor,
    Game,
    Server,
};

struct EngineHostContext
{
    // 项目模块会通过 EngineModuleContext 读到宿主类型，所以必须在加载任何模块之前确定。
    EngineHostKind host = EngineHostKind::Editor;
};

// 运行时可以为宿主启动的模块/服务。配置里给的是“要启哪几个”，不是一堆开关，
// 这样以后加服务只需要加枚举项，宿主侧的启动代码不用跟着改。
enum class EngineRuntimeModule : uint32_t
{
    Audio,       // 音频设备与 AudioSystem
    Input,       // 输入上下文与 LocalInputSystem
    Render,      // 渲染服务：Render2D和Render3D
};

// 运行时启动配置。服务按能力创建：服务器、纯逻辑进程可以把音频/输入/3D 从
// modules 里去掉，那样 Runtime 不会去初始化对应的后端。
struct EngineRuntimeConfig
{
    EngineHostContext host;
    // 默认全开；宿主想省掉某个服务就从这里删掉对应枚举。
    std::vector<EngineRuntimeModule> modules = {
        EngineRuntimeModule::Audio,
        EngineRuntimeModule::Input,
        EngineRuntimeModule::Render,
    };

    // 查询某个服务是否在启动列表里。
    bool HasModule(EngineRuntimeModule module) const
    {
        return std::find(modules.begin(), modules.end(), module) != modules.end();
    }
};

// 引擎对外创建 Vulkan 上下文时的额外要求。引擎必需的扩展会先加，这里只补充宿主
// 自己那条路径需要的东西（比如宿主 UI 后端的实例扩展）。
struct EngineRenderContextDesc
{
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;

    // 离屏目标尺寸。填了就在建上下文时一并建离屏目标（宿主只管把结果合成出去）；
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

    // 引擎自己创建 Vulkan 实例与设备。只建设备，不建窗口、不建交换链：渲染目标由
    // Renderer3D 按宿主要求创建，画面怎么用是宿主的事。宿主随后用 GetVulkanContext()
    // 拿句柄去接自己的 UI 后端。
    bool CreateRenderContext(const EngineRenderContextDesc& desc, std::string* error = nullptr);
    // 当前上下文句柄。引擎还没有上下文时 valid 为 false。
    EngineVulkanContext GetVulkanContext() const;

    // 渲染服务：3D / 2D 都在 Renderer 里，下面两个访问器是给宿主的捷径。
    Render::Renderer* GetRenderer();
    const Render::Renderer* GetRenderer() const;

    // 启动列表里没有 Renderer3D 时返回 nullptr，调用方需要自己判空。
    Render::Renderer3D* GetRenderer3D();
    const Render::Renderer3D* GetRenderer3D() const;

    // 2D 能力：D3D11 设备与交换链由宿主创建并持有，引擎只借用句柄建 D2D 设备上下文。
    // 之后的 2D 内容（精灵/文字/GIF/调试线）统一走 Renderer2D 提交。
    bool CreateRenderer2D(ID3D11Device* device, IDXGISwapChain1* swapChain, std::string* error = nullptr);
    Render::Renderer2D* GetRenderer2D();
    const Render::Renderer2D* GetRenderer2D() const;

    // 音频设备归引擎所有（启动列表里带 Audio 时才有），宿主拿它设音量、播 BGM，
    // 不要自己再造一个设备实例。
    std::shared_ptr<AudioDevice> GetAudioDevice() const { return m_audioDevice; }

    // 场景正在引擎的场景线程上推进时为真（播放态）。
    bool IsSceneOpen() const noexcept { return m_world != nullptr; }
    bool IsSceneThreaded() const noexcept { return m_world && m_world->isRunning(); }

    // 播放态：引擎按场景自己的逻辑帧率跑一个场景线程，宿主只开关它。
    // 编辑/暂停态：宿主每帧调 StepEditorFrame，引擎做命令处理、零步物理、把引擎
    // 输入帧映射到受控相机，再跑相机跟随与渲染帧数据后处理。这样宿主不需要知道
    // World 里装了哪些系统，也不用自己写一遍输入到 Controller 的搬运。
    void StartSceneThread(float logicDeltaMilliseconds, float fixedDeltaMilliseconds);
    void StopSceneThread();
    void StepEditorFrame(float deltaMilliseconds);

    // 3D 帧消费者的目标帧率（0 表示不限帧）
    void SetTargetFps(uint32_t fps);
    uint32_t GetTargetFps() const;

    // 项目模块属于引擎而不是宿主：只加载一次，之后每个新打开的场景都会在宿主
    // 填充内容之前注册好模块系统。宿主不需要接触 EngineModuleManager。
    bool LoadModules(const std::string& directory, std::string* error = nullptr);

    // 场景装载挂钩：建 World、注册系统、解析 GUID、反序列化都由引擎做，宿主只在
    // 前后各插一手。编辑器用它把自由相机塞进场景、再按场景里存的视角恢复相机参数，
    // 引擎本身不需要知道这些编辑器专属实体。
    struct SceneOpenHooks
    {
        // 反序列化之前调用：宿主要加的实体必须在这一步进 World，才能被 scene 里的
        // 实体一起接收第一帧更新。
        std::function<bool(World&)> prepare;
        // 反序列化之后调用：此时场景内容已经在 World 里，适合按场景 settings 修正
        // 宿主自己那部分状态。
        std::function<void(World&, const SceneCodec&)> finish;
    };

    // 工程也属于引擎：场景加载要用工程里的资产库做 GUID 解析，宿主不需要自己管
    // ProjectManager，只要 OpenProject 之后再 OpenScene。
    bool OpenProject(const std::string& projectFilePath, std::string* error = nullptr);
    void CloseProject();
    bool IsProjectOpen() const;
    ProjectManager* GetProjectManager() const;

    // 先建一个候选 World 交给宿主配置/填充，只有在初始化成功时才把它装成当前场景。
    bool OpenEmptyScene(const std::function<bool(World&)>& initializeScene);

    // 一个调用打开场景：sceneAsset 既可以是场景资源的 GUID，也可以是工程内的资源路径。
    // 两种写法都先走资产库定位，再反序列化成 World 装成当前场景。
    bool OpenScene(const std::string& sceneAsset, const SceneOpenHooks& hooks = {}, std::string* error = nullptr);

    // 已经拿到 SceneCodec 时走这里（例如宿主自己缓存了场景快照）。资源解析仍然用
    // 引擎的工程资产库，所以宿主不必再写一遍 GUID 解析。
    bool OpenScene(const std::shared_ptr<SceneCodec>& scene, const SceneOpenHooks& hooks = {}, std::string* error = nullptr);

    void CloseScene();
    World* GetWorld() const noexcept { return m_world.get(); }

    // ---- 场景切换：登记 / 执行两段式 ----
    // 任何线程都能 Request，它只是登记"想切过去"的意图，立刻返回。真正的切换必须由
    // 宿主在帧边界调 PumpSceneChanges：那里才能安全地 join 场景线程、重建 World。
    // 场景线程自己不能执行切换——它会 join 到自己，那是死锁。
    void RequestSceneChange(const std::string& sceneAsset, SceneOpenHooks hooks = {});
    bool HasPendingSceneChange() const noexcept { return m_hasPendingScene; }
    // 有登记就切一次、并按原节拍重启场景线程；返回是否真的切了。宿主每帧调一次。
    bool PumpSceneChanges(std::string* error = nullptr);

    // UI 命令总线：宿主/编辑器用它做校验和诊断（GetCommands / PendingCount）；
    // 模块拿到的是 ModuleHostServices 里的同一个指针。
    UI::UICommandBus& GetUICommands() noexcept { return m_uiCommands; }
    // 上一次 Pump 的结果，没人认领的命令名都在里面，给宿主/编辑器做诊断。
    const UI::PumpReport& GetLastUiPumpReport() const noexcept { return m_lastPumpReport; }

    std::shared_ptr<EngineInputContext> GetInputContext() const noexcept { return m_inputContext; }

private:
    void RegisterBaseSystems(World& world);
    void BindVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer);
    void SceneThreadLoop();
    // 只在 Shutdown 里用：模块必须在它们供给的场景销毁之后才卸载。
    void UnloadModules();

    // 渲染线程的 sink 落点：只入队，不派发。
    void OnUiEvents(const std::vector<UI::UiEvent>& events);
    // 场景线程每帧调：把队列里的 UI 事件派发给模块的 handler。
    void PumpUiEvents();
    // 模块经 ModuleHostServices 回调进来的切场景入口（调用线程不限）。
    static void OnModuleRequestSceneChange(void* owner, const char* sceneAsset);

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
    UI::UICommandBus m_uiCommands;
    UI::PumpReport m_lastPumpReport;

    // 交给模块的宿主服务。它必须活得比模块久（模块随时可能回调进来，包括切场景）。
    NComponentCodec::ModuleHostServices m_hostServices;

    // 待执行的场景切换。登记和执行分开，中间只隔一个帧边界。
    std::mutex m_pendingSceneMutex;
    std::string m_pendingSceneAsset;
    SceneOpenHooks m_pendingSceneHooks;
    std::atomic<bool> m_hasPendingScene{ false };

    // 启动场景线程时用的步长，切完场景要按同样节拍重启。
    float m_logicDeltaMilliseconds = 16.0f;
    float m_fixedDeltaMilliseconds = 16.0f;
};
