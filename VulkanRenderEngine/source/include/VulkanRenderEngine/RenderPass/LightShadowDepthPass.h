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

		uint32_t oneSideCommandSize = 0;
		uint32_t twoSideCommandSize = 0;

		std::shared_ptr<StorageBlock> ssbo_StaticMesh_TransformAndMaterialIndices;
		std::shared_ptr<StorageBlock> ssbo_StaticMesh_WorldAABB;

		std::shared_ptr<IndirectBufferBlock> oneSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
		std::shared_ptr<IndirectBufferBlock> twoSideIndirectCommandBuffer = std::make_shared<IndirectBufferBlock>();

		std::shared_ptr<StorageBlock> ssbo_dirLightMeta = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_dirLightCascade = std::make_shared<StorageBlock>();
		std::shared_ptr<StorageBlock> ssbo_dirLightCascadeDistances = std::make_shared<StorageBlock>();
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

		std::shared_ptr<IndirectBufferBlock> oneSideInidrectCommands;
		std::shared_ptr<IndirectBufferBlock> twoSideInidrectCommands;

		PreRecordData(PreRecordLightType t) :type(t) {}
		PreRecordData(PreRecordData&& other) {
			type = other.type;
			shadowMatrices = std::move(other.shadowMatrices);
			viewPorts = std::move(other.viewPorts);

			ssbo_ShadowMatrices = std::move(other.ssbo_ShadowMatrices);
			ssbo_LightProps = std::move(other.ssbo_LightProps);
			other.ssbo_ShadowMatrices = std::make_shared<StorageBlock>();
			other.ssbo_LightProps = std::make_shared<StorageBlock>();

			oneSideInidrectCommands = std::move(other.oneSideInidrectCommands);
			twoSideInidrectCommands = std::move(other.twoSideInidrectCommands);
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
		std::shared_ptr<GraphicsPipeline>& shader_Skinned,
		GraphicsBindingRecord& shader_StaticMesh_Binding,
		GraphicsBindingRecord& shader_Skinned_Binding
	);

private:
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthSkinnedShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthSkinnedShader;
	std::shared_ptr<ComputePipeline> _frustumCullingShader;
};