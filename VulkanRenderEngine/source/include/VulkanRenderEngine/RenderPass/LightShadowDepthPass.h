#pragma once

#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "RenderPassBase.h"


class LightShadowDepthPass :public RenderPassBase
{
private:
	struct SelfContext {
		std::shared_ptr<AtlasMap> atlas = std::make_shared<AtlasMap>();

		std::shared_ptr<StorageBlock> ssbo_ShadowMatrices = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_LightProps = std::make_shared<StorageBlock>();

		std::vector<IndirectDrawCommand> staticMesh_OneSideCommands;
		std::vector<IndirectDrawCommand> staticMesh_TwoSideCommands;

		IndirectBufferBlock oneSideCommandBuffer;
		IndirectBufferBlock twoSideCommandBuffer;

		std::shared_ptr<StorageBlock> ssbo_dirLightMeta = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_dirLightCascade = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_pointLightMeta = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_spotLightMeta = std::make_shared<StorageBlock>();
	};


public:
	LightShadowDepthPass(
		const std::string& dirLightShadowStaticMeshVertexShaderPath,
		const std::string& dirLightShadowSkinnedMeshVertexShaderPath,
		const std::string& dirLightShadowFragmentShaderPath,
		const std::string& pointLightShadowStaticMeshVertexShaderPath,
		const std::string& pointLightShadowSkinnedMeshVertexShaderPath,
		const std::string& pointLightShadowFragmentShaderPath
	);
	virtual ~LightShadowDepthPass();
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void CalculateShadowAtlas(RenderState& state, AtlasMap& atlas);

	void processDirAndSpotLight(SelfContext& ctx, RenderState& state, const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd, DynamicRenderInfo& renderInfo);
	void processPointLight(SelfContext& ctx, RenderState& state, const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd, DynamicRenderInfo& renderInfo);

	void RenderSceneLightShadowPassSceneInstance(
		SelfContext& ctx,
		const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
		RenderState& state,
		std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
		std::shared_ptr<GraphicsPipeline>& shader_Skinned,
		GraphicsBindingRecord& shader_StaticMesh_Binding,
		GraphicsBindingRecord& shader_Skinned_Binding,
		uint32_t count,
		std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes,
		std::vector<VKRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& skinned,
		DynamicRenderInfo& renderInfo,
		std::vector<DynamicViewport>& viewPorts
	);

	void SetupLightingData(SelfContext& ctx, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state);

private:
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthSkinnedShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthSkinnedShader;
};