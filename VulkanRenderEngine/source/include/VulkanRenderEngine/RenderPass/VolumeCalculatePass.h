#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "RenderPassBase.h"
#include "VulkanRenderEngine/General/TAAFilter.h"
#include "VulkanRenderEngine/General/AtrousGaussFilter.h"

class VolumeCalculatePass :public RenderPassBase
{

public:
	VolumeCalculatePass(
		const std::string& lutShaderPath,
		const std::string& combinShaderPath
	);
	virtual ~VolumeCalculatePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void OnResize(uint32_t width, uint32_t height) override;	// 历史纹理按新尺寸重新分配，下一帧清历史
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);


private:
	struct FrameRenderData
	{
		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gDepthStencil;

		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> gPrevPosition;
		std::shared_ptr<Texture2D> gPrevNormal;
		std::shared_ptr<Texture2D> gPrevDepthStencil;

		std::shared_ptr<Texture2D> targetSceneColor;

		std::shared_ptr<Texture2D> tempScatterColor;
		std::shared_ptr<Texture2D> tempTramsmittance;
		std::shared_ptr<Texture2D> tempSceneColor;

		std::shared_ptr<Texture2D> taaScatterColorTexture;
		std::shared_ptr<Texture2D> taaGaussScatterColorTexture;

		std::shared_ptr<Texture2D> taaHistoryScatterColorTexture;

		bool atmoEnable = false;
		bool exponentialHeightFogEnable = false;
		bool volumeFogEnable = false;
		bool cloudEnable = false;

		// 散射图是否需要 TAA + 高斯滤波（大气 / 体积雾 / 体积云任一开启时散射才有噪声）。
		// Execute 决定跑不跑滤波、CombinVolume 决定读哪张散射图，都只看这一个值，避免两处条件不一致
		bool scatterFiltered = false;
	};

	void DrawLut(std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,RenderGraph::FrameDataRegistry& registry,RenderState& state,FrameRenderData& data);
	bool DrawTAAFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);
	bool DrawGaussFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);
	void CombinVolume(std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,RenderGraph::FrameDataRegistry& registry,RenderState& state,FrameRenderData& data);

	void SetTAAEnable(bool enable) const;

private:
	ComputePipeline _lutShader;
	ComputePipeline _combinShader;

	// 云噪声只加载一次，整个生命周期复用
	std::shared_ptr<Texture3D> _cloudShapeNoise;
	std::shared_ptr<Texture2D> _blueNoise;

	TAAFilter _taaFilter;
	AtrousGaussFilter _gaussFilter;

	mutable bool _firstDrawTemporal;
	mutable bool _taaEnable;
};
