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
	};

	void DrawLut(std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,RenderGraph::FrameDataRegistry& registry,RenderState& state,FrameRenderData& data);
	bool DrawTAAFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);
	bool DrawGaussFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);
	void CombinVolume(std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,RenderGraph::FrameDataRegistry& registry,RenderState& state,FrameRenderData& data);

	void SetEnable(bool enable) const;

private:
	ComputePipeline _lutShader;
	ComputePipeline _combinShader;

	TAAFilter _taaFilter;
	AtrousGaussFilter _gaussFilter;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;
};
