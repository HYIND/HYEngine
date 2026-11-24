#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"
#include "VulkanRenderEngine/General/IndirectUpsample.h"

class DDGISharedState;

class SSRPass :public RenderPassBase
{
public:
	SSRPass(const std::string& computerShaderPath);

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void OnResize(uint32_t width, uint32_t height) override;	// 历史纹理按新尺寸重新分配，下一帧清历史

	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);	// 命中点采样 DDGI 用

private:
	struct FrameRenderData
	{
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

		std::shared_ptr<Texture2D> sceneColorMap;
		std::shared_ptr<Texture2D> hzbDepthMap;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> temporalAccumulateColorTexture;
		std::shared_ptr<Texture2D> temporalAccumulateMomentTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTempTexture;
		std::shared_ptr<Texture2D> denoisedTexture;			// 空域滤波结果（已去调制），乘回镜面反射率后写到 outPutTexture

		std::shared_ptr<Texture2D> temporalAccumulateHistoryColorTexture;
		std::shared_ptr<Texture2D> temporalAccumulateHistoryMomentTexture;

		std::shared_ptr<Texture2D> ddgiIrradianceAtlas;
		std::shared_ptr<Texture2D> ddgiDepthAtlas;
		std::shared_ptr<Texture2D> ddgiProbeData;
	};

	bool DrawSSR(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state);
	bool DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawRemodulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

private:
	ComputePipeline _ssrShader;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;

	std::shared_ptr<UniformBlock> _SSRParamsUBO;
	std::shared_ptr<DDGISharedState> _sharedState;

	TemporalAccumulate _temporalAccumulate;
	AtrousBilateralFilter _spatialDenoisingFilter;
	IndirectUpsample _remodulate;		// 同分辨率：乘回镜面反射率
};