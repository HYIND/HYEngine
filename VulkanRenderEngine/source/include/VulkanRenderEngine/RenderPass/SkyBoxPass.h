#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "RenderPassBase.h"

class SkyBoxPreCalculatePass :public RenderPassBase
{
public:
	SkyBoxPreCalculatePass();
	virtual ~SkyBoxPreCalculatePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	IBLImageBuilder _iblBuilder;

	std::shared_ptr<TextureCube> _skyCubeDiffuse;
	std::shared_ptr<TextureCube> _skyCubePrefilter;
	std::shared_ptr<Texture2D> _brdfLUT;

	std::shared_ptr<TextureCube> _lastSkyCube;
};

class SkyBoxPass :public RenderPassBase
{
public:
	SkyBoxPass(const std::string& computeShaderPath);
	virtual ~SkyBoxPass();
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _shader;
};