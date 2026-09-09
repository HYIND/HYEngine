#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class LightingPass :public RenderPassBase
{
public:
	LightingPass(const std::string& computeShaderPath);
	~LightingPass();
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);;

private:
	ComputePipeline _shader;
};