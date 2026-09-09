#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SkyBoxPass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

SkyBoxPass::SkyBoxPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.computePath = computeShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddStorageImage(0)
		.AddUnifromTexture(1)
		.AddUnifromTexture(2);

	if (config.Validate())
		_shader.Create(config);

}

SkyBoxPass::~SkyBoxPass()
{
}

bool SkyBoxPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.skyboxOn && state.skybox.cube;
}

void SkyBoxPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	auto skyboxCubeMap = state.skybox.cube;
	if (!skyboxCubeMap)
		return;

	auto colorBuffer = ctx.GetExternal(0);
	auto depthBuffer = ctx.GetExternal(1);
	auto cubeMap = state.skybox.cube;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	_shader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_shader.SetStorageImage(colorBuffer, 0);
	_shader.SetUniformTexture(depthBuffer, 1);
	_shader.SetUniformTextureCube(state.skybox.cube, 2);

	_shader.Bind(cmd);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}