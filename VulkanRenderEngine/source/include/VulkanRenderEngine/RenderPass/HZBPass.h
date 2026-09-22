#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "RenderPassBase.h"

class HZBPass :public RenderPassBase
{
public:
	HZBPass(
		const std::string& depthVertexShaderPath,
		const std::string& depthfragmentShaderPath,
		const std::string& HZBComputerShaderPath,
		const std::string& occlusionCullingComputerShaderPath
	);

	~HZBPass();

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	uint32_t GetMaxLevel() const;

private:
	void DrawDepthMap(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, RenderState& state);
	void DrawHZB(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, RenderState& state);
	void GetOcclusionCulling(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& HZBMap, RenderState& state);

private:
	std::shared_ptr<GraphicsPipeline> _depthShader;
	std::shared_ptr<ComputePipeline> _HZBShader;
	std::shared_ptr<ComputePipeline> _occlusionCullShader;
	uint32_t _maxLevel = 7;
};