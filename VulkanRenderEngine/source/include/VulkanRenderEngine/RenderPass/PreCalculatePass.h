#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "RenderPassBase.h"
#include <vector>

class PreCalculatePass :public RenderPassBase
{
public:
	PreCalculatePass();
	~PreCalculatePass();

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& stat);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void AnlysisIndirectCommands(RenderState& state, std::vector<TransAndMaterialIndex>& staticMesh_TransformAndMaterialIndices);

};