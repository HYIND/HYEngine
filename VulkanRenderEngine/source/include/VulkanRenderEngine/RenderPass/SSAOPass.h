#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SSAOPass :public RenderPassBase
{

public:
	SSAOPass(
		const std::string& ssaoComputeShaderPath,
		const std::string& ssaoBlurComputeShaderPath
	);
	virtual ~SSAOPass();
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _ssaoShader;
	ComputePipeline _ssaoBlurShader;

	std::shared_ptr<Texture2D> _noiseTexture;
	std::shared_ptr<UniformBlock> _ssaoParams;
};