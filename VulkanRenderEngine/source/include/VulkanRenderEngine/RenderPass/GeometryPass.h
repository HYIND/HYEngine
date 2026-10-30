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
		const std::string& skinnedMeshVertexShaderPath = "shader/gbuffer/geometrypass_SkinnedMesh.vs"
	);
	~GeometryPass();

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
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

	bool SetupStaticBufferData(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		GraphicsBindingRecord& binding,
		std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem>& items,
		VKRenderObjectData::RenderIndex& renderIndex,
		std::vector<IndirectDrawCommand>& oneSideCommands,
		std::vector<IndirectDrawCommand>& twoSideCommands
	);
	void RenderSceneGeometryPassStatic(RenderGraph::FrameDataRegistry& registry, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort);

private:
	std::shared_ptr<GraphicsPipeline> _staticShader;
	// 蒙皮网格用另一套 VS：除了静态那套，还要从上一帧的蒙皮顶点缓冲取重投影位置
	std::shared_ptr<GraphicsPipeline> _skinnedShader;
	// 没有上一帧顶点缓冲时占位用的假缓冲（重投影关闭时 shader 不会读它）
	std::shared_ptr<VertexBufferBlock> _dummySkinnedVertexBuffer;
};