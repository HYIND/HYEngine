#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class GeometryPass :public RenderPassBase
{
public:
	GeometryPass(
		const std::string& staticMeshVertexShaderPath,
		const std::string& staticMeshFragmentShaderPath,
		const std::string& skinnedMeshvertexShaderPath,
		const std::string& skinnedFragmentShaderPath
	);
	~GeometryPass();

	virtual void EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);;

	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	DynamicRenderInfo GenerateDynamicRenderInfo(
		RenderState& state,
		std::shared_ptr<Texture2D>& gPosition,
		std::shared_ptr<Texture2D>& gNormal,
		std::shared_ptr<Texture2D>& gAlbedoOpacity,
		std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
		std::shared_ptr<Texture2D>& gMotionVectorMap,
		std::shared_ptr<Texture2D>& gEmission,
		std::shared_ptr<Texture2D>& tempDepthStencilMap
	);

	void SetupIndirecDrawMaterial(RenderState& state);
	bool SetupStaticBufferData(
		std::shared_ptr<GraphicsPipeline>& shader,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& items,
		OpenGLRenderObjectData::RenderIndex& renderIndex,
		std::vector<IndirectDrawCommand>& oneSideCommands,
		std::vector<IndirectDrawCommand>& twoSideCommands
	);
	void RenderSceneGeometryPassStatic(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort);
	void RenderSceneGeometryPassSkinned(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort);

private:
	std::shared_ptr<GraphicsPipeline> _staticShader;
	std::shared_ptr<GraphicsPipeline> _skinnedShader;

	std::shared_ptr<IndirectBufferBlock> _oneSideCommandBuffer;
	std::shared_ptr<IndirectBufferBlock> _twoSideCommandBuffer;
};