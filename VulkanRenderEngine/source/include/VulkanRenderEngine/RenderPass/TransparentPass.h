#pragma once

#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class TransparentPass : public RenderPassBase
{
public:
	TransparentPass(
		const std::string& vertexShaderPath,
		const std::string& fragmentShaderPath
	);
	virtual ~TransparentPass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;

private:
	void SortStaticItems(RenderState& state);

private:
	std::shared_ptr<GraphicsPipeline> _shader;

	std::shared_ptr<StorageBlock> _renderDataBlock;			// static 网格的 curTransform/prevTransform/materialIndex
	std::vector<IndirectDrawCommand> _commands;				// 已按由远到近排好序的静态命令
	std::vector<GeometryRenderData> _renderData;			// 与 _commands 一一对应
	std::vector<size_t> _sortedItemIndex;					// 排好序的 transparentItems 下标
};
