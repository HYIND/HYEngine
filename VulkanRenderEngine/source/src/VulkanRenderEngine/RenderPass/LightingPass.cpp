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
		.AddUnifromTexture(7);

	_shader.Create(config);
}

LightingPass::~LightingPass() {}

void LightingPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void LightingPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto gMetallicRoughness = ctx.GetInput(3);
	auto atlasShadowMap = ctx.GetInput(4);
	auto ssao = ctx.GetInput(5);
	auto gEmission = ctx.GetInput(6);

	auto target = ctx.GetExternal(0);

	_shader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	_shader.SetStorageImage(target, 0);
	_shader.SetUniformTexture(gPosition, 1);
	_shader.SetUniformTexture(gNormal, 2);
	_shader.SetUniformTexture(gAlbedoOpacity, 3);
	_shader.SetUniformTexture(gMetallicRoughness, 4);
	_shader.SetUniformTexture(gEmission, 5);
	_shader.SetUniformTexture(ssao, 6);
	_shader.SetUniformTexture(atlasShadowMap, 7);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	RenderHelp::SetupLightingData(
		cmd,
		_shader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	_shader.Bind(cmd);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}
