#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DepthFogPass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

struct DepthFogParams
{
	glm::vec3 fogColor;
	float fogHeight;
	float fogDistanceFalloff;   // 距离衰减
	float fogHeightFalloff;     // 高度衰减系数
};


DepthFogPass::DepthFogPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.computePath = computeShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddUnifromTexture(2)
		.AddUnifromTexture(3);

	if (config.Validate())
		_shader.Create(config);

	_paramsUBO = std::make_shared<UniformBlock>(sizeof(DepthFogParams));
	_binding.SetUniformBlock(_paramsUBO, 0);
}

bool DepthFogPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.depthFogOn;
}

void DepthFogPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	DepthFogParams params
	{
		.fogColor = state.option.depthFogParams.fogColor,
		.fogHeight = state.option.depthFogParams.fogHeight,
		.fogDistanceFalloff = state.option.depthFogParams.fogDistanceFalloff,
		.fogHeightFalloff = state.option.depthFogParams.fogHeightFalloff
	};

	_paramsUBO->WriteData(&params, sizeof(DepthFogParams));
}

void DepthFogPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto sceneColorBuffer = ctx.GetExternal(0);
	auto sceneDepthBuffer = ctx.GetExternal(1);

	auto tempColor = ctx.GetTemp(0);

	if (!sceneColorBuffer
		|| !sceneDepthBuffer
		|| !tempColor
		|| sceneColorBuffer->IsEmpty()
		|| sceneDepthBuffer->IsEmpty()
		|| tempColor->IsEmpty()
		)
		return;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColor);

	uint32_t width = sceneColorBuffer->GetWidth();
	uint32_t height = sceneColorBuffer->GetHeight();


	_binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_binding.SetStorageImage(sceneColorBuffer, 1);
	_binding.SetUniformTexture(tempColor, 2);
	_binding.SetUniformTexture(sceneDepthBuffer, 3);

	_shader.Bind(cmd, _binding);
	cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}