#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "RenderPassBase.h"

class AtomspherePass :public RenderPassBase
{
public:
	AtomspherePass(const std::string& computeShaderPath);
	virtual ~AtomspherePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _shader;
};