#pragma once
#include "vkstdafx.h"

#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"

#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/SkinnedPass.h"
#include "VulkanRenderEngine/RenderPass/BloomPass.h"
#include "VulkanRenderEngine/RenderPass/CombinPass.h"
#include "VulkanRenderEngine/RenderPass/GlobalPostProcessPass.h"

#include "VulkanRenderEngine/SharedTexture.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/DDGIDebug.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"

struct alignas(16) camera_compData
{
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projView;
    alignas(16) glm::mat4 invProjection;
    alignas(16) glm::mat4 invView;
    alignas(16) glm::mat4 invProjView;
    alignas(16) glm::vec3 invTransViewRow1;
    alignas(16) glm::vec3 invTransViewRow2;
    alignas(16) glm::vec3 invTransViewRow3;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 direction;
    alignas(16) glm::vec3 directionUp;
    alignas(16) glm::vec3 directionRight;
    float nearPlane = 0.1f;
    float farPlane = 1000.f;
    float fov = 80.f;
};

// 3D 渲染器（Renderer3D 的 Vulkan 后端）。引擎只提供离屏模式：每帧画到 finalColorBuffer，
// 由宿主通过 WaitImage / FetchImage 取走后自己呈现（编辑器走 ImGui 窗口，游戏走 D3D11 交换链）。
class VKRenderer3D
{
public:
    // 提供CreateInstance扩展，用于创建vk实例，实例用于初始化VKRenderer3D
    static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(const std::vector<std::string>& extensionNames);
    static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(uint32_t extensionCount, const char** extensionNames);

    static std::shared_ptr<VKRenderer3D> CreateForOffScreen(std::shared_ptr<VKCore::VulkanInstance> instance, uint32_t width, uint32_t height);					// 创建离屏模式

    // 用于分段初始化
    // 仅创建设备、不初始化渲染目标
    static std::shared_ptr<VKRenderer3D> CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance);
    static std::shared_ptr<VKRenderer3D> CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance, const std::vector<std::string>& extraDeviceExtensions);

    // 分段初始化
    // 初始化渲染目标
    static bool CreateForOffScreen_Target(std::shared_ptr<VKRenderer3D>& renderer, uint32_t width, uint32_t height);

public:
    VKRenderer3D();
    ~VKRenderer3D();

    glm::u32vec2 GetSize() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    Scene3DRenderOption GetOption() const;

    void SetOption(Scene3DRenderOption option);
    void Resize(uint32_t width, uint32_t height);

    // 关掉后整帧不跑 3D 渲染图（省 GPU），输出图清成黑色，2D 层 / UI 照常叠在上面。
    // 切换时要等在途的帧画完，下一帧起生效
    void SetSceneRenderEnabled(bool enabled) { _sceneRenderEnabled.store(enabled, std::memory_order_release); }
    bool IsSceneRenderEnabled() const { return _sceneRenderEnabled.load(std::memory_order_acquire); }

    void PushFrameState(std::shared_ptr<RenderState>& state);
    // 候选队列里是否还有没被 ExecuteLoop 取走的帧；帧循环据此决定要不要重复提交旧数据
    bool HasPendingFrameState();

    void WaitImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback);
    bool FetchImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback);

    void Run();
    void Stop();
    void ExecuteLoop();

    uint32_t GetMaxFramesInFlight() const;

public:
    std::shared_ptr<VKCore::VulkanInstance> GetVulkanInstance() const;
    std::shared_ptr<VKCore::VulkanDevice> GetVulkanDevice() const;

    // DDGI 调试通道（编辑器 <-> 渲染器），跨渲染图重建保持不变
    std::shared_ptr<DDGIDebugChannel> GetDDGIDebugChannel();

    // 渲染性能统计通道（编辑器「调试 → 渲染性能」）：不开启时各打点处只多一次判空
    const std::shared_ptr<RenderProfilerChannel>& GetRenderProfiler() const { return _renderProfiler; }

private:
    void InitForOffSceen(uint32_t width, uint32_t height);
    void Init_Internal();

    void InitRenderTarget();
    void InitRenderGraph();
    void InitSceneRenderGraph();
    void InitFirstPersonRenderGraph();

private:
    struct RenderTargetData {
        std::shared_ptr<Texture2D> sceneColorBuffer;
        std::shared_ptr<Texture2D> firstPersonColorBuffer;
        std::shared_ptr<Texture2D> combinColorBuffer, combinBrightColorBuffer;
        std::vector<std::shared_ptr<Texture2D>> bloomMipBuffers;
        std::shared_ptr<Texture2D> finalColorBuffer;
    };

    struct FrameData {
        std::shared_ptr<RenderGraph::Graph::FilghtSync> sync;
        std::shared_ptr<RenderTargetData> renderTarget;
        std::shared_ptr<RenderState> state;
        std::shared_ptr<ThreadPool::SubmitHandle<void>> handle;
        // 在帧提交渲染时（串行分发点）解析出的相机数据：
        // prev 来自上一张真正提交过的帧，cur 是本帧。各帧各持一份，互不干扰
        camera_compData cameraPrevData{};
        camera_compData cameraCurData{};
        bool skipScene = false;     // 3D 关闭：这一帧只清输出图
    };

    void DrawOffScreen(const std::shared_ptr<FrameData>& data);
    void DrawEmptyFrame(const std::shared_ptr<FrameData>& data);

private:
    void ResolveFrameHistory(const std::shared_ptr<FrameData>& data);

    camera_compData BuildCameraCompData(const RenderState& state) const;

    void SetupRenderState(const std::shared_ptr<FrameData>& data);
    void SetupIndirectDrawData(std::shared_ptr<RenderState>& state);

    void FinishRendering(std::shared_ptr<RenderState>& state);

    //void RenderFirstPersonLayer(RenderState& state);

private:
    uint32_t scr_width;
    uint32_t scr_height;

    std::shared_ptr<VKCore::VulkanInstance> _vulkanInstance;
    std::shared_ptr<VKCore::VulkanDevice> _vulkanDevice;


    //// FirstPersonLayer
    //std::unique_ptr<FirstPersonPass> _firstPersonPass;

    std::unique_ptr<PreCalculatePass> _preCalculatePass;
    std::unique_ptr<SkinnedPass> _skinnedPass;
    std::unique_ptr<CombinPass> _combinPass;
    std::unique_ptr<BloomPass> _globalBloomPass;
    std::unique_ptr<GlobalPostProcessPass> _globalPostProcessPass;

    std::vector<std::shared_ptr<RenderTargetData>> _renderTargets;

    // 上一张真正提交渲染的帧的相机数据
    camera_compData _cameraCache{};
    bool _cameraCacheValid = false;

    struct {
        uint32_t frameIndex = 0;
        float prevEV100 = 1.0f;
    }_record;

    std::unique_ptr<RenderGraph::Graph> _sceneRenderGraph;

    // 场景渲染图的 Pass：第一次构建渲染图时创建，resize 按新尺寸重建渲染图时挂到新图上复用
    struct ScenePasses
    {
        std::shared_ptr<RenderPassBase> atmospherePreCalculate, skyboxPreCalculate, hzb, lightingShadowDepth, geometry, ssao, lighting, skyBox;
        std::shared_ptr<RenderPassBase> rayTraceGeneral, rayTraceReflect, rayTraceGI, ssr, ssgi;
        std::shared_ptr<RenderPassBase> ddgiUpdate, ddgiBlend, ddgiApply, ddgiProbeDraw;
        std::shared_ptr<RenderPassBase> indirectDiffuseCombin, indirectSpecularCombin, indirectPathTracingCombin, combinIndirectLighting;
        std::shared_ptr<RenderPassBase> lightDraw, volumeLut, volumeCombine, transparent, effect, autoExposure, gBufferHistoryCopy;
        uint32_t hzbMaxLevel = 1;

        template<typename F>
        void ForEach(F&& f)
        {
            for (auto* pass : {
                &atmospherePreCalculate, &skyboxPreCalculate, &hzb, &lightingShadowDepth, &geometry, &ssao, &lighting, &skyBox,
                &rayTraceGeneral, &rayTraceReflect, &rayTraceGI, &ssr, &ssgi,
                &ddgiUpdate, &ddgiBlend, &ddgiApply, &ddgiProbeDraw,
                &indirectDiffuseCombin, &indirectSpecularCombin, &indirectPathTracingCombin, &combinIndirectLighting,
                &lightDraw, &volumeLut, &volumeCombine, &transparent, &effect, &autoExposure, &gBufferHistoryCopy })
            {
                if (*pass)
                    f(**pass);
            }
        }
    };
    std::unique_ptr<ScenePasses> _scenePasses;

    Scene3DRenderOption _option;

    std::shared_ptr<DDGIDebugChannel> _ddgiDebugChannel;
    std::shared_ptr<RenderProfilerChannel> _renderProfiler = std::make_shared<RenderProfilerChannel>();


    uint32_t _maxFramesInFlight = 1;  // 最大在途帧数

    std::queue<std::shared_ptr<RenderState>> _candidateFrameStates;
    SpinLock _candidateFrameStatesMutex;

    SpinLock _executeMutex;
    ConditionVariable _executeCV;
    std::queue<std::shared_ptr<FrameData>> _runningFrames;

    RecursiveSpinLock _doneFramesMutex;
    ConditionVariable _doneFramesCV;
    std::queue<std::shared_ptr<FrameData>> _doneFrames;

    ThreadPool _frameTaskPool;
    bool _stop = true;
    std::atomic<bool> _sceneRenderEnabled{ true };
    std::shared_ptr<std::thread> _exeLoopThread;
};