#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "RenderPassBase.h"

class SkinnedPass :public RenderPassBase
{
public:
	SkinnedPass(const std::string& skinnedComputeShaderPass = "shader/skinned/skinning.comp");
	~SkinnedPass();

	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

public:
	void Execute(const std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, RenderState& state);

private:
	struct SkinningDispatch
	{
		uint32_t vertexCount;
		uint32_t sourceVertexOffset;
		uint32_t skinnedDataOffset;
		uint32_t paletteOffset;
		uint32_t paletteCount;
		uint32_t outputVertexOffset;
	};

	std::vector<IndirectDrawCommand> AnlysisSkinnedIndirectCommands(
		const std::vector<VKRenderObjectData::SceneSkinnedRenderData::SkinnedMeshItem>& items,
		std::vector<VKRenderContext::BoneMatrices>& paletteMatrices,
		std::vector<SkinningDispatch>& dispatches,
		std::vector<TransAndMaterialIndex>& skinnedMesh_TransformAndMaterialIndices,
		std::vector<SkinnedMotionData>& motionDatas,
		uint32_t commandStartOffset,
		uint32_t& outputVertexOffset,
		FrameHistoryData* frameHistory,				// 本帧：登记每个 pose 的骨架规模与网格顶点区间
		const FrameHistoryData* prevFrameHistory	// 上一帧：查重投影来源
	);

private:
	uint32_t SkinningWorkgroupSize = 64;
	ComputePipeline _skinningShader;
};