#pragma once

#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"

class RTCoreRayTraceGIPass :public RenderPassBase
{
public:
	RTCoreRayTraceGIPass(
		const std::string& raygenPath,
		const std::string& missPath,
		const std::string& closestHitPath,
		const std::string& anyHitPath,
		const std::string& intersectionPath,
		const std::string& callablePath,
		const std::string& spatialDenoisingComputerShaderPath,
		const std::string& temporalAccumulateComputerShaderPath,
		const std::string& scaleComputerShaderPath
	);
	~RTCoreRayTraceGIPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer);

private:
	struct FrameRenderData
	{
		glm::ivec2 drawSize;
		glm::ivec2 scrSize;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gDepthStencil;

		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> ssaoMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> gPrevPosition;
		std::shared_ptr<Texture2D> gPrevNormal;
		std::shared_ptr<Texture2D> gPrevDepthStencil;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> temporalAccumulateColorTexture;
		std::shared_ptr<Texture2D> temporalAccumulateMomentTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTempTexture;

		std::shared_ptr<Texture2D> temporalAccumulateHistoryColorTexture;
		std::shared_ptr<Texture2D> temporalAccumulateHistoryMomentTexture;
	};

	bool DrawRayTraceGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

	bool BindAccelerationStructure(RayTracingBindingRecord& binding);

private:
	RayTracingPipeline _rayTraceShader;
	ComputePipeline _scaleShader;

	RayTracingBindingRecord _rayTraceShaderBinding;
	ComputeBindingRecord _scaleShaderBinding;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<UniformBlock> _RayTraceParamsUBO;

	TemporalAccumulate _temporalAccumulate;
	AtrousBilateralFilter _spatialDenoisingFilter;
};
