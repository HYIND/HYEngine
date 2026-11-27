#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/EditorViewport.h"

// 编辑器拾取：把实体 ID 缓冲（GeometryPass 的编辑器附件，R32_UINT，存 entityId + 1，0 = 没有物体）里
// 请求矩形内的像素读回 CPU，整理成实体 id 发布到 EditorViewportChannel；点选且 wantPosition 时顺带读回命中像素的
// 世界坐标（gPosition），拖放资源摆放物体用。
// 读回用计算着色器（shader/editor/pickReadback.comp）以 SHADER_READ_ONLY 采样，和 SSAO / 光照等读 gPosition 的 Pass 布局一致，
// 渲染图不给同一资源的多个读者排序，这样才不会互相改布局。
// 只有编辑器有请求的那一帧执行，同步读回（只卡这一帧）；平时 ShouldExecute 为 false，没有开销。
// 只在 GlobalConfig::EditorViewportFeatures 打开时创建（见 VKRenderer3D::InitSceneRenderGraph）。
//   Input 0：实体 ID 缓冲；Input 1：gPosition（两者都声明成 computeReadLayout，由渲染图切布局）
class EditorPickPass :public RenderPassBase
{
public:
	explicit EditorPickPass(const std::string& computeShaderPath = "shader/editor/pickReadback.comp");
	~EditorPickPass() override = default;

	void SetChannel(std::shared_ptr<EditorViewportChannel> channel) { _channel = std::move(channel); }

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	ComputePipeline _shader;
	std::shared_ptr<EditorViewportChannel> _channel;
	std::shared_ptr<StorageBlock> _readback;		// 计算着色器输出 + 读回源，不够大时换一个更大的
};
