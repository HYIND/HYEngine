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

	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

public:
	void Execute(const std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, RenderState& state);

private:
	void AnlysisIndirectCommands(RenderState& state, std::vector<TransAndMaterialIndex>& staticMesh_TransformAndMaterialIndices, std::vector<AABB>& worldAABBs);
};