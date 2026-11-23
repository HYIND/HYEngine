#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 全屏采样：输出已乘 albedo 的漫反射间接光，格式同 rayTraceGI_Output / ssgi_Output。
// 多体积按优先级混合。漏光调试打开时，记录调试像素用到的探针与权重，同步读回后交给编辑器。
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
	void PublishLeak(const DDGIFrame& frame, const DDGILeakRecordGPU& record);

private:
	ComputePipeline _shader;
	ComputePipeline _leakShader;		// 带漏光调试记录的变体

	std::shared_ptr<DDGISharedState> _sharedState;
	std::shared_ptr<StorageBlock> _leakBuffer;
};
