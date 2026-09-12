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
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void CalculateShadowAtlas(RenderState& state);
	void processDirAndSpotLight(RenderState& state, DynamicRenderInfo& renderInfo);
	void processPointLight(RenderState& state, DynamicRenderInfo& renderInfo);

	void RenderSceneLightShadowPassSceneInstance(
		RenderState& state,
		std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
		std::shared_ptr<GraphicsPipeline>& shader_Skinned,
		uint32_t count,
		std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes,
		std::vector<VKRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& skinned,
		DynamicRenderInfo& renderInfo,
		std::vector<DynamicViewport>& viewPorts
	);

private:
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _dirLightShadowDepthSkinnedShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthStaticMeshShader;
	std::shared_ptr<GraphicsPipeline> _pointLightShadowDepthSkinnedShader;

	bool useAMDViewportExt;

	bool _shouldUpdateTexture;

	int64_t _lastUpadteTime;
	uint32_t updateFrameDelta = 1;		//每3帧更新一次

	std::shared_ptr<AtlasMap> _atlas;

	struct
	{
		std::vector<std::shared_ptr<DirLightInfo>> dirLightInfos;
		std::vector<std::shared_ptr<PointLightInfo>> pointLightInfos;
		std::vector<std::shared_ptr<SpotLightInfo>> spotLightInfos;
	}_history;

	std::shared_ptr<StorageBlock> _ssbo_ShadowMatrices;
	std::shared_ptr<StorageBlock> _ssbo_LightProps;

	std::shared_ptr<IndirectBufferBlock> _oneSideCommandBuffer;
	std::shared_ptr<IndirectBufferBlock> _twoSideCommandBuffer;

	std::vector<glm::mat4> _staticMesh_Transforms;
};