#pragma once

#include "VulkanRenderEngine/RenderPass/RayTraceGeneralPass.h"
#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"

class RayTraceGIPass :public RenderPassBase
{
public:
	RayTraceGIPass(
		const std::string& rayTraceComputerShaderPath,
		const std::string& spatialDenoisingComputerShaderPath,
		const std::string& temporalDenoisingComputerShaderPath,
		const std::string& scaleComputerShaderPath
	);
	~RayTraceGIPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer);

private:
	struct FrameRenderData
	{
		glm::ivec2 drawSize;
		glm::ivec2 scrSize;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> sceneDepthBuffer;
		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> ssaoMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> historyColorTexture;
	};

	bool DrawRayTraceGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawTemporalDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

	bool BindGeneralData(ComputeBindingRecord& binding);

private:
	ComputePipeline _rayTraceShader;
	ComputePipeline _spatialDenoisingShader;
	ComputePipeline _temporalDenoisingShader;
	ComputePipeline _scaleShader;

	ComputeBindingRecord _rayTraceBinding;
	ComputeBindingRecord _spatialDenoisingBinding;
	ComputeBindingRecord _temporalDenoisingBinding;
	ComputeBindingRecord _scaleBinding;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;

	std::shared_ptr<RayTraceGeneralBuffer> _buffers;

	std::shared_ptr<UniformBlock> _RayTraceParamsUBO;
	std::shared_ptr<UniformBlock> _SpatialDenoisingParamsUBO;
	std::shared_ptr<UniformBlock> _TemporalAccumulateParamsUBO;
};
