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
		const std::string& fragmentShaderPath,
		const std::string& oitVertexShaderPath,
		const std::string& oitFragmentShaderPath,
		const std::string& oitCompositeVertexShaderPath,
		const std::string& oitCompositeFragmentShaderPath
	);
	virtual ~TransparentPass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;

private:
	void PrepareStaticItems(RenderState& state, bool sorted);
	void ExecuteSorted(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	void ExecuteOit(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

	void UploadStaticData(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);
	bool HasDrawableItems(RenderState& state) const;

private:
	std::shared_ptr<GraphicsPipeline> _sortedShader;		// Sorted：单附件前向着色
	std::shared_ptr<GraphicsPipeline> _oitShader;			// OIT：accum + reveal 两个附件
	std::shared_ptr<GraphicsPipeline> _oitCompositeShader;	// OIT：全屏合成

	std::shared_ptr<StorageBlock> _renderDataBlock;			// static 网格的 curTransform/prevTransform/materialIndex
	std::vector<IndirectDrawCommand> _commands;				// 已按由远到近排好序的静态命令
	std::vector<GeometryRenderData> _renderData;			// 与 _commands 一一对应
	std::vector<size_t> _sortedItemIndex;					// 排好序的 transparentItems 下标
};
