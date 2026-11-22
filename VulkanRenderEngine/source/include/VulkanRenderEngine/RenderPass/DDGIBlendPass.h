#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 光线 -> 探针：余弦卷积、深度矩、时域混合、边框回填，并回拷到持久 history。
// dispatch = (1, 1, probeCount)，一个 workgroup 负责一个探针。
class DDGIBlendPass : public RenderPassBase
{
public:
	DDGIBlendPass(const std::string& computePath);
	~DDGIBlendPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);

private:
	ComputePipeline _shader;

	std::shared_ptr<DDGISharedState> _sharedState;
	std::shared_ptr<UniformBlock> _paramsUBO;

	DDGIFrameState _frame{};
	bool _framePrepared = false;

	bool _enable = false;
};
