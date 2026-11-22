#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 全屏采样：输出已乘 albedo 的漫反射间接光，格式同 rayTraceGI_Output / ssgi_Output。
class DDGIApplyPass :public RenderPassBase
{
public:
	DDGIApplyPass(const std::string& computePath);
	~DDGIApplyPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);

private:
	ComputePipeline _shader;

	std::shared_ptr<DDGISharedState> _sharedState;
	std::shared_ptr<UniformBlock> _paramsUBO;

	bool _framePrepared = false;
	bool _enable = false;
};
