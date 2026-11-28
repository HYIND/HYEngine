#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SkyBoxPreCalculatePass :public RenderPassBase
{
public:
	SkyBoxPreCalculatePass();
	virtual ~SkyBoxPreCalculatePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	std::shared_ptr<TextureCube> _lastSkyCube;
	uint64_t _skyCubeRevision = 0;	// 天空盒资产切换时递增，跨帧保存在 Pass 里（RenderState 每帧新建）
};

class SkyBoxPass :public RenderPassBase
{
public:
	SkyBoxPass(const std::string& computeShaderPath);
	virtual ~SkyBoxPass();
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _shader;
};