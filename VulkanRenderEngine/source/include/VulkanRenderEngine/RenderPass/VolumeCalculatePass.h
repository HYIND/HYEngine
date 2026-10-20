#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "RenderPassBase.h"
#include "AtmospherePreCalculatePass.h"

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
	void DrawLut(
		std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
		RenderGraph::FrameDataRegistry& registry,
		RenderState& state,
		std::shared_ptr<Texture2D>& tempScatterColor,
		std::shared_ptr<Texture2D>& tempTramsmittance,
		std::shared_ptr<Texture2D>& sceneDepthBuffer,
		std::shared_ptr<Texture2D>& atlasShadowMap,
		std::shared_ptr<Texture2D>& gPosition,
		bool atmoEnable
	);

	void CombinVolume(
		std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
		RenderGraph::FrameDataRegistry& registry,
		RenderState& state,
		std::shared_ptr<Texture2D>& tempScatterColor,
		std::shared_ptr<Texture2D>& tempTramsmittance,
		std::shared_ptr<Texture2D>& sceneDepthBuffer,
		std::shared_ptr<Texture2D>& atlasShadowMap,
		std::shared_ptr<Texture2D>& gPosition,
		bool atmoEnable
	);

private:
	ComputePipeline _lutShader;
	ComputePipeline _combinShader;
};
