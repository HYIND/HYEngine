#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "RenderPassBase.h"

class DepthFogPass :public RenderPassBase
{
public:
	DepthFogPass(const std::string& computeShaderPath);
	virtual ~DepthFogPass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);

private:
	ComputePipeline _shader;
	std::shared_ptr<UniformBlock> _paramsUBO;
};