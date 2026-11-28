#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SkyBoxPass.h"


SkyBoxPreCalculatePass::SkyBoxPreCalculatePass() = default;

bool SkyBoxPreCalculatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return (!state.option.flags.atmosphereOn || state.lights.dirLightInfos.empty())
		&& state.option.flags.skyboxOn && state.skyboxParams.skyCube;
}

void SkyBoxPreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	// 天空盒 cube 是加载进来的资产，本 Pass 不再派生 IBL；只上报源与内容版本，
	// IBL 由 IBLPreCalculatePass 在 Execute 里统一生成。
	auto& skyCube = state.skyboxParams.skyCube;
	if (_lastSkyCube != skyCube)
	{
		_lastSkyCube = skyCube;
		++_skyCubeRevision;
	}

	state.skyboxParams.hasSkyBoxPreData = true;
	state.skyboxParams.skyCubeRevision = _skyCubeRevision;
}

void SkyBoxPreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{}

SkyBoxPass::SkyBoxPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
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
{}

bool SkyBoxPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return 
		(!state.option.flags.atmosphereOn || state.lights.dirLightInfos.empty())
		&& state.option.flags.skyboxOn && state.skyboxParams.skyCube;
}

void SkyBoxPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto skyboxCubeMap = state.skyboxParams.skyCube;
	if (!skyboxCubeMap)
		return;

	auto colorBuffer = ctx.GetExternal(0);
	auto depthBuffer = ctx.GetFrameLocal(0);

	auto cmd = cmdCtx.GetCmd();

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(colorBuffer, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(depthBuffer, vk::ImageAspectFlagBits::eDepth, 1);
	binding.SetUniformTextureCube(skyboxCubeMap, vk::ImageAspectFlagBits::eColor, 2);

	_shader.Bind(cmd, binding);
	cmd->dispatch((state.framebuffer.width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (state.framebuffer.height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

