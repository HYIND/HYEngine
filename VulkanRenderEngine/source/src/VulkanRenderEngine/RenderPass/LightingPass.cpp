#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightingPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/AtlasMap.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

LightingPass::LightingPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.computePath = computeShaderPath;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);

	config
		.AddCameraUnifromDataBinding()
		.AddLightDataBinding()
		.AddStorageImage(0)
		.AddUnifromTexture(1)
		.AddUnifromTexture(2)
		.AddUnifromTexture(3)
		.AddUnifromTexture(4)
		.AddUnifromTexture(5)
		.AddUnifromTexture(6)
		.AddUnifromTexture(7)
		.AddUnifromTexture(8);

	_shader.Create(config);
}

LightingPass::~LightingPass() {}

void LightingPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void LightingPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto gMetallicRoughness = ctx.GetInput(3);
	auto atlasShadowMap = ctx.GetInput(4);
	auto ssao = ctx.GetInput(5);
	auto gEmission = ctx.GetInput(6);
	auto gDepthStencilMap = ctx.GetInput(7);

	auto target = ctx.GetExternal(0);

	ComputeBindingRecord binding;

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	binding.SetStorageImage(target, 0);
	binding.SetUniformTexture(gPosition, 1);
	binding.SetUniformTexture(gNormal, 2);
	binding.SetUniformTexture(gAlbedoOpacity, 3);
	binding.SetUniformTexture(gMetallicRoughness, 4);
	binding.SetUniformTexture(gEmission, 5);
	binding.SetUniformTexture(ssao, 6);
	binding.SetUniformTexture(atlasShadowMap, 7);
	binding.SetUniformTexture(gDepthStencilMap, 8);

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta
	);

	auto cmd = VKCONTEXT->GetCommandBuffer();
	_shader.Bind(cmd, binding);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}
