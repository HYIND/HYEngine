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
#include "VulkanRenderEngine/RenderPass/EditorOutlinePass.h"

#include "VulkanRenderEngine/SharedTexture.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKFramePipeline.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/DDGIDebug.h"
#include "VulkanRenderEngine/General/EditorViewport.h"
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

// 3D 渲染器（Renderer3D 的 Vulkan 后端），是 VKFramePipeline 的一个阶段：
// 画场景、做后处理，结果写到这一帧的最终画面（FrameJob::finalColor），后面的阶段（2D / UI）再往上叠。
// 帧循环、在途 / 完成队列、取图都在 VKFramePipeline 里；实例和设备在 VKContext 里。
class VKRenderer3D : public IFrameStage
{
public:
    VKRenderer3D();
    ~VKRenderer3D() override;

    glm::u32vec2 GetSize() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    Scene3DRenderOption GetOption() const;

    void SetOption(Scene3DRenderOption option);

    // 关掉后整帧不跑 3D 渲染图（省 GPU），输出图清成黑色，2D 层 / UI 照常叠在上面。
    // 下一帧起生效；关掉的帧照样按段进出流水线，前后帧不用等它
    void SetSceneRenderEnabled(bool enabled) { _sceneRenderEnabled.store(enabled, std::memory_order_release); }
    bool IsSceneRenderEnabled() const { return _sceneRenderEnabled.load(std::memory_order_acquire); }

public:
    // IFrameStage
    //   OnResize   - 第一次建尺寸无关的 Pass；每次重建中间目标（flightCount 份）和场景渲染图
    //   OnTick     - 清理渲染图里闲置的瞬时资源
    //   OnDispatch - 派发线程上：挂这一帧的中间目标、接上一帧的蒙皮历史、推进相机历史
    //   Execute    - 帧线程上：每帧固定进出 8 段（见 VKRenderer3D.cpp）
    void OnResize(uint32_t width, uint32_t height, uint32_t flightCount) override;
    void OnTick() override;
    void OnDispatch(FrameJob& job, const FrameJob* prev, uint32_t stageIndex) override;
    void Execute(FrameJob& job, uint32_t stageIndex) override;

public:
    // DDGI 调试通道（编辑器 <-> 渲染器），跨渲染图重建保持不变
    std::shared_ptr<DDGIDebugChannel> GetDDGIDebugChannel();

    // 编辑器视口通道（拾取等），跨渲染图重建保持不变。没开 GlobalConfig::EditorViewportFeatures 时
    // 也能取到，只是没有 Pass 处理里面的请求
    std::shared_ptr<EditorViewportChannel> GetEditorViewportChannel() const { return _editorViewportChannel; }

private:
    void Init_Internal();

    void InitRenderTarget();
    void InitRenderGraph();
    void InitSceneRenderGraph();

private:
    // 3D 自己的中间目标，每个在途帧一份（按 FrameJob::flightIndex 取），只在帧内用，帧做完就可以给下一帧。
    // 最终画面（RGBA8，后处理写、2D / UI 往上叠、宿主取走）不在这里，归流水线（FrameJob::finalColor）
    struct RenderTargetData {
        std::shared_ptr<Texture2D> sceneColorBuffer;
        std::shared_ptr<Texture2D> firstPersonColorBuffer;
        std::shared_ptr<Texture2D> combinColorBuffer, combinBrightColorBuffer;
        std::vector<std::shared_ptr<Texture2D>> bloomMipBuffers;
        // 编辑器选中遮罩（EditorSelectionMaskPass 写、EditorOutlinePass 读）：要活到渲染图之后的后处理段，所以归这里。
        // 只在 GlobalConfig::EditorViewportFeatures 打开时创建
        std::shared_ptr<Texture2D> editorSelectionMask;
    };

    // 挂在 FrameJob 上的 3D 每帧数据
    struct Frame3DData : FrameStageData {
        std::shared_ptr<RenderTargetData> renderTarget;
        // 在帧进流水线时（串行派发点）解析出的相机数据：
        // prev 来自上一张真正画过场景的帧，cur 是本帧。各帧各持一份，互不干扰
        camera_compData cameraPrevData{};
        camera_compData cameraCurData{};
        bool skipScene = false;     // 3D 关闭：这一帧各段都是空段，只在后处理段把最终画面清黑
    };

private:
    void ResolveFrameHistory(RenderState& state, Frame3DData& data);

    camera_compData BuildCameraCompData(const RenderState& state) const;

    void SetupRenderState(RenderState& state, const Frame3DData& data);
    void SetupIndirectDrawData(std::shared_ptr<RenderState>& state);

    static void ClearColorTexture(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& texture, const vk::ClearColorValue& clearColor);

    //void RenderFirstPersonLayer(RenderState& state);

private:
    uint32_t scr_width;
    uint32_t scr_height;
    uint32_t _targetCount = 1;  // 中间目标份数 = 最大在途帧数，由流水线在 OnResize 时给


    //// FirstPersonLayer
    //std::unique_ptr<FirstPersonPass> _firstPersonPass;

    std::unique_ptr<PreCalculatePass> _preCalculatePass;
    std::unique_ptr<SkinnedPass> _skinnedPass;
    std::unique_ptr<CombinPass> _combinPass;
    std::unique_ptr<BloomPass> _globalBloomPass;
    std::unique_ptr<GlobalPostProcessPass> _globalPostProcessPass;
    std::unique_ptr<EditorOutlinePass> _editorOutlinePass;     // 只在 GlobalConfig::EditorViewportFeatures 打开时创建

    std::vector<std::shared_ptr<RenderTargetData>> _renderTargets;

    // 上一张真正提交渲染的帧的相机数据
    camera_compData _cameraCache{};
    bool _cameraCacheValid = false;

    struct {
        float prevEV100 = 1.0f;
    }_record;

    std::unique_ptr<RenderGraph::Graph> _sceneRenderGraph;

    // 场景渲染图的 Pass：第一次构建渲染图时创建，resize 按新尺寸重建渲染图时挂到新图上复用
    struct ScenePasses
    {
        std::shared_ptr<RenderPassBase> atmospherePreCalculate, skyboxPreCalculate, iblPreCalculate, hzb, lightingShadowDepth, geometry, ssao, lighting, skyBox;
        std::shared_ptr<RenderPassBase> rayTraceGeneral, rayTraceReflect, rayTraceGI, ssr, ssgi;
        std::shared_ptr<RenderPassBase> ddgiUpdate, ddgiBlend, ddgiApply, ddgiProbeDraw;
        std::shared_ptr<RenderPassBase> indirectDiffuseCombin, indirectSpecularCombin, indirectPathTracingCombin, combinIndirectLighting;
        std::shared_ptr<RenderPassBase> lightDraw, volumeLut, volumeCombine, transparent, effect, autoExposure, gBufferHistoryCopy;
        // 只在 GlobalConfig::EditorViewportFeatures 打开时创建
        std::shared_ptr<RenderPassBase> editorPick, editorSelectionMask;
        uint32_t hzbMaxLevel = 1;

        template<typename F>
        void ForEach(F&& f)
        {
            for (auto* pass : {
                &atmospherePreCalculate, &skyboxPreCalculate, &iblPreCalculate, &hzb, &lightingShadowDepth, &geometry, &ssao, &lighting, &skyBox,
                &rayTraceGeneral, &rayTraceReflect, &rayTraceGI, &ssr, &ssgi,
                &ddgiUpdate, &ddgiBlend, &ddgiApply, &ddgiProbeDraw,
                &indirectDiffuseCombin, &indirectSpecularCombin, &indirectPathTracingCombin, &combinIndirectLighting,
                &lightDraw, &volumeLut, &volumeCombine, &transparent, &effect, &autoExposure, &gBufferHistoryCopy, &editorPick, &editorSelectionMask })
            {
                if (*pass)
                    f(**pass);
            }
        }
    };
    std::unique_ptr<ScenePasses> _scenePasses;

    Scene3DRenderOption _option;

    std::shared_ptr<DDGIDebugChannel> _ddgiDebugChannel;

    const std::shared_ptr<EditorViewportChannel> _editorViewportChannel = std::make_shared<EditorViewportChannel>();

    std::atomic<bool> _sceneRenderEnabled{ true };
};
