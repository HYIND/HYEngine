#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIBlendPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/GlobalConfig.h"

DDGIBlendPass::DDGIBlendPass(const std::string& computePath)
{
	ComputePipelineConfig config;
	config.computePath = computePath;
	DDGIAddLayoutDefines(config);
	config
		.AddUnifromTexture(0)	// rayData
		.AddUnifromTexture(1)	// 辐照度 history
		.AddUnifromTexture(2)	// 深度 history
		.AddStorageImage(3)		// 辐照度图集
		.AddUnifromBuffer(4)	// DDGIUniforms
		.AddStorageImage(5)		// 深度图集
		.AddStorageImage(6);	// 每探针累积次数（持久，原地读写）

	if (config.Validate())
		_shader.Create(config);

	_paramsUBO = std::make_shared<UniformBlock>(sizeof(DDGIUniforms));
}

DDGIBlendPass::~DDGIBlendPass()
{}

bool DDGIBlendPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return _enable && _sharedState != nullptr;
}

void DDGIBlendPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state) || !_framePrepared)
		return;

	auto rayData = ctx.GetInput(0);
	auto irradianceHistory = ctx.GetPersitent(0);
	auto depthHistory = ctx.GetPersitent(1);
	auto probeState = ctx.GetPersitent(2);
	auto irradianceOut = ctx.GetOutput(0);
	auto depthOut = ctx.GetOutput(1);

	if (!rayData || rayData->IsEmpty()
		|| !irradianceHistory || irradianceHistory->IsEmpty() || !depthHistory || depthHistory->IsEmpty()
		|| !probeState || probeState->IsEmpty()
		|| !irradianceOut || irradianceOut->IsEmpty() || !depthOut || depthOut->IsEmpty())
		return;

	auto cmd = cmdCtx.GetCmd();

	const DDGIUniforms uniforms = DDGIBuildUniforms(_frame, state.option.ddgiParams, 0u, state.camera.position);
	_paramsUBO->WriteData(&uniforms, sizeof(uniforms));

	rayData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	irradianceHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	depthHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	probeState->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	irradianceOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	depthOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetUniformTexture(rayData, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(irradianceHistory, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(depthHistory, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetStorageImage(irradianceOut, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformBlock(_paramsUBO, 4);
	binding.SetStorageImage(depthOut, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetStorageImage(probeState, vk::ImageAspectFlagBits::eColor, 6);

	_shader.Bind(cmd, binding);
	cmd->dispatch(1, 1, _frame.volume.totalProbes);

	// 回拷 history，供下一帧混合与多次反弹。不能让同一张图同帧既读又写
	Texture2D::CopyTextureAsync(cmd, irradianceOut, irradianceHistory);
	Texture2D::CopyTextureAsync(cmd, depthOut, depthHistory);

	cmd->SubmitToQueue();
}

void DDGIBlendPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_enable = state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable;
	_framePrepared = false;

	if (!ShouldExecute(registry, state))
		return;

	_frame = _sharedState->Resolve(
		state.renderRecord.frameIndex,
		state.option.ddgiParams,
		state.sceneBounds.min,
		state.sceneBounds.max,
		state.sceneBounds.valid != 0,
		state.camera.position
	);
	_framePrepared = _frame.volume.totalProbes > 0;
}

void DDGIBlendPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}
