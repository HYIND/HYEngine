#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 光线 -> 探针：余弦卷积、深度矩、时域混合、边框回填、自动重定位。
// 图集按世界格子滚动寻址、原地更新：dispatch = (本帧更新数, 1, 1)，只处理更新列表里的探针，没轮到的不动，没有 history 回拷。
// 编辑器请求「烘焙重定位」时，混合之后再用 DDGIBakeCopy.comp 把该体积每个探针的 (偏移, n) 拷进回读缓冲并同步读回。
class DDGIBlendPass : public RenderPassBase
{
public:
	DDGIBlendPass(const std::string& computePath, const std::string& bakeCopyPath = "shader/DDGI/DDGIBakeCopy.comp");
	~DDGIBlendPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);

private:
	ComputePipeline _shader;
	ComputePipeline _bakeShader;

	std::shared_ptr<DDGISharedState> _sharedState;
};
