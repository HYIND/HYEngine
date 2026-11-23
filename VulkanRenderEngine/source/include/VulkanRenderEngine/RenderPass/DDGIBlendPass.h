#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 光线 -> 探针：余弦卷积、深度矩、时域混合、边框回填、自动重定位，并回拷到持久 history。
// dispatch = (probesPerRow, 行数, 1)，一个 workgroup 负责一个槽位；没轮到的槽位从 history 的旧位置搬过来。
// 编辑器请求「烘焙重定位」时，把该体积每个探针的 (偏移, n) 写进回读缓冲并同步读回。
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
	std::shared_ptr<StorageBlock> _dummyBake;
};
