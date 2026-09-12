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

	virtual void EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);;

	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& stat);

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	std::shared_ptr<StorageBlock> _ssbo_StaticMesh_TransformAndMaterialIndices;
	std::shared_ptr<IndirectBufferBlock> _indirectCommandBuffer;
};