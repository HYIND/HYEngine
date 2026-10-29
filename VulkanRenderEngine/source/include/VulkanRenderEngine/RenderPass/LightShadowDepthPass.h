#pragma once

#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
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

		uint32_t staticOneSideCommandSize = 0;
		uint32_t staticTwoSideCommandSize = 0;
		uint32_t skinnedOneSideCommandSize = 0;
		uint32_t skinnedTwoSideCommandSize = 0;

		std::shared_ptr<StorageBlock> ssbo_StaticMesh_TransformAndMaterialIndices;
		std::shared_ptr<StorageBlock> ssbo_StaticMesh_WorldAABB;
		std::shared_ptr<StorageBlock> ssbo_SkinnedMesh_TransformAndMaterialIndices;
		std::shared_ptr<VertexBufferBlock> skinnedMesh_VertexBuffer;

		std::shared_ptr<IndirectBufferBlock> oneSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
		std::shared_ptr<IndirectBufferBlock> twoSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
		std::shared_ptr<IndirectBufferBlock> skinnedOneSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
		std::shared_ptr<IndirectBufferBlock> skinnedTwoSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();

		std::shared_ptr<StorageBlock> ssbo_dirLightMeta = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_dirLightCascade = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_dirLightCascadeDistances = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_pointLightMeta = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_spotLightMeta = std::make_shared<StorageBlock>();
	};


public:
	LightShadowDepthPass(
		const std::string& dirLightShadowStaticMeshVertexShaderPath,
		const std::string& dirLightShadowFragmentShaderPath,
		const std::string& pointLightShadowStaticMeshVertexShaderPath,
		const std::string& pointLightShadowFragmentShaderPath
	);
	virtual ~LightShadowDepthPass();
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	void CalculateShadowAtlas(RenderState& state, AtlasMap& atlas);
	void SetupLightingData(SelfContext& ctx, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state);

private:
	enum class PreRecordLightType { Dir = 0, Point };
	struct PreRecordData {
		PreRecordLightType type;
		std::vector<glm::mat4> shadowMatrices;
		std::vector<DynamicViewport> viewPorts;

		std::shared_ptr<StorageBlock> ssbo_ShadowMatrices = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_LightProps = std::make_shared<StorageBlock>();

		std::shared_ptr<IndirectBufferBlock> staticOneSideInidrectCommands;
		std::shared_ptr<IndirectBufferBlock> staticTwoSideInidrectCommands;
		std::shared_ptr<IndirectBufferBlock> skinnedOneSideIndirectCommands;
		std::shared_ptr<IndirectBufferBlock> skinnedTwoSideIndirectCommands;

		PreRecordData(PreRecordLightType t) :type(t) {}
		PreRecordData(PreRecordData&& other) {
			type = other.type;
			shadowMatrices = std::move(other.shadowMatrices);
			viewPorts = std::move(other.viewPorts);

			ssbo_ShadowMatrices = std::move(other.ssbo_ShadowMatrices);
			ssbo_LightProps = std::move(other.ssbo_LightProps);
			other.ssbo_ShadowMatrices = std::make_shared<StorageBlock>();
			other.ssbo_LightProps = std::make_shared<StorageBlock>();

			staticOneSideInidrectCommands = std::move(other.staticOneSideInidrectCommands);
			staticTwoSideInidrectCommands = std::move(other.staticTwoSideInidrectCommands);
			skinnedOneSideIndirectCommands = std::move(other.skinnedOneSideIndirectCommands);
			skinnedTwoSideIndirectCommands = std::move(other.skinnedTwoSideIndirectCommands);
		}
	};

	void PreRecordDirAndSpotLight(std::vector<PreRecordData>& datas, SelfContext& ctx, RenderState& state);
	void PreRecordPointLight(std::vector<PreRecordData>& datas, SelfContext& ctx, RenderState& state);
	void PreRecordCulling(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		SelfContext& ctx,
		PreRecordData& data,
		std::vector<bool>& shouldCullings
	);
	void PreRecordRenderScene(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		SelfContext& ctx,
		PreRecordData& data,
		DynamicRenderInfo& renderInfo,
		std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
		GraphicsBindingRecord& shader_StaticMesh_Binding,
		GraphicsBindingRecord& shader_SkinnedMesh_Binding
	);

	void NewFunction(LightShadowDepthPass::SelfContext& ctx, std::shared_ptr<UniformBlock>& sideParamsUBO, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, DynamicRenderInfo& renderInfo, std::vector<DynamicViewport>& viewPorts, bool& hasClear, vk::ClearAttachment& clearAttachment, std::vector<vk::ClearRect>& clearRects, std::shared_ptr<IndirectDrawManager>& manager, LightShadowDepthPass::PreRecordData& data);

private:
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthStaticMeshShader;
	std::shared_ptr<ComputePipeline> _frustumCullingShader;
};