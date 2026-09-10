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
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);
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

	bool DrawRayTraceGI(FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(FrameRenderData& data, RenderState& state);
	bool DrawTemporalDenoising(FrameRenderData& data, RenderState& state);
	bool DrawScale(FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

	bool BindGeneralData(ComputePipeline& shader);

private:
	ComputePipeline _rayTraceShader_useGbuffer;
	ComputePipeline _spatialDenoisingShader;
	ComputePipeline _temporalDenoisingShader;
	ComputePipeline _scaleShader;

	mutable bool _firstDrawTemporal; 
	mutable bool _enable;

	std::shared_ptr<RayTraceGeneralBuffer> _buffers;

	std::shared_ptr<UniformBlock> _RayTraceParamsUBO;
	std::shared_ptr<UniformBlock> _SpatialDenoisingParamsUBO;
	std::shared_ptr<UniformBlock> _TemporalAccumulateParamsUBO;
};
