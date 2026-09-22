#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/Shader.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class TransparentPass :public RenderPassBase
{
public:
	TransparentPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~TransparentPass() = default;

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	void SetupIndirecDrawMaterial(RenderState& state);

private:
	Shader _shader;
};