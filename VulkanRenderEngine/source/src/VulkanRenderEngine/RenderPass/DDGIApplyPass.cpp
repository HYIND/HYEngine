#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIApplyPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/GlobalConfig.h"

DDGIApplyPass::DDGIApplyPass(const std::string& computePath)
{
	ComputePipelineConfig config;
	config.computePath = computePath;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	DDGIAddLayoutDefines(config);
	config
		.AddStorageImage(0)			// 输出
		.AddUnifromTexture(1)		// gPosition
		.AddUnifromTexture(2)		// gNormal
		.AddUnifromTexture(3)		// gAlbedoOpacity
		.AddUnifromTexture(4)		// SSAO
		.AddUnifromTexture(5)		// 深度
		.AddUnifromTexture(6)		// 辐照度图集
		.AddUnifromTexture(7)		// 深度图集
		.AddUnifromBuffer(8);		// DDGIUniforms

	if (config.Validate())
		_shader.Create(config);

	_paramsUBO = std::make_shared<UniformBlock>(sizeof(DDGIUniforms));
}

DDGIApplyPass::~DDGIApplyPass()
{}

bool DDGIApplyPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return _enable && _sharedState != nullptr;
}

void DDGIApplyPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state) || !_framePrepared)
		return;

	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto ssaoMap = ctx.GetInput(3);
	auto irradianceAtlas = ctx.GetInput(4);
	auto depthAtlas = ctx.GetInput(5);
	auto gDepthStencil = ctx.GetFrameLocal(0);
	auto outImage = ctx.GetOutput(0);

	if (!outImage || outImage->IsEmpty()
		|| !irradianceAtlas || irradianceAtlas->IsEmpty() || !depthAtlas || depthAtlas->IsEmpty()
		|| !gPosition || gPosition->IsEmpty() || !gNormal || gNormal->IsEmpty()
		|| !gAlbedoOpacity || gAlbedoOpacity->IsEmpty() || !ssaoMap || ssaoMap->IsEmpty()
		|| !gDepthStencil || gDepthStencil->IsEmpty())
		return;

	auto cmd = cmdCtx.GetCmd();

	outImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetStorageImage(outImage, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(gPosition, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(gNormal, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(ssaoMap, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(gDepthStencil, vk::ImageAspectFlagBits::eDepth, 5);
	binding.SetUniformTexture(irradianceAtlas, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(depthAtlas, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformBlock(_paramsUBO, 8);

	_shader.Bind(cmd, binding);

	auto size = outImage->GetSize();
	cmd->dispatch(
		(size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1
	);

	cmd->SubmitToQueue();
}

void DDGIApplyPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_enable = state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable;
	_framePrepared = false;

	if (!ShouldExecute(registry, state))
		return;

	const DDGIFrameState frame = _sharedState->Resolve(
		state.renderRecord.frameIndex,
		state.option.ddgiParams,
		state.sceneBounds.min,
		state.sceneBounds.max,
		state.sceneBounds.valid != 0,
		state.camera.position
	);
	if (frame.volume.totalProbes == 0)
		return;

	const DDGIUniforms uniforms = DDGIBuildUniforms(frame, state.option.ddgiParams, 0u, state.camera.position);
	_paramsUBO->WriteData(&uniforms, sizeof(uniforms));
	_framePrepared = true;
}

void DDGIApplyPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}
