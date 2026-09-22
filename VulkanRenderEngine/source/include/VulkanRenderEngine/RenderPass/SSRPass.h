#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SSRPass :public RenderPassBase
{
public:
	SSRPass(
		const std::string& computerShaderPath,
		const std::string& spatialDenoisingComputerShaderPath,
		const std::string& temporalDenoisingComputerShaderPath
	);

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetEnable(bool enable) const;

private:
	struct FrameRenderData
	{
		glm::ivec2 scrSize;
		glm::ivec2 drawSize;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> historyColorTexture;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> colorMap;
		std::shared_ptr<Texture2D> depthMap;
		std::shared_ptr<Texture2D> hzbDepthMap;
	};

	bool DrawSSR(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawTemporalDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

private:
	ComputePipeline _ssrShader;
	ComputePipeline _spatialDenoisingShader;
	ComputePipeline _temporalDenoisingShader;

	ComputeBindingRecord _ssgiShaderBinding;
	ComputeBindingRecord _spatialDenoisingShaderBinding;
	ComputeBindingRecord _temporalDenoisingShaderBinding;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;

	std::shared_ptr<UniformBlock> _SSRParamsUBO;
	std::shared_ptr<UniformBlock> _SpatialDenoisingParamsUBO;
	std::shared_ptr<UniformBlock> _TemporalAccumulateParamsUBO;
};