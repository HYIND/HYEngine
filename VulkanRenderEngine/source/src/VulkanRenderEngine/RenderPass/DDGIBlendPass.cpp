#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIBlendPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/GlobalConfig.h"

namespace
{
	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kFrameKey = "ddgiBlendFrame";
}

DDGIBlendPass::DDGIBlendPass(const std::string& computePath, const std::string& bakeCopyPath)
{
	{
		ComputePipelineConfig config;
		config.computePath = computePath;
		DDGIAddLayoutDefines(config);
		config
			.AddUnifromTexture(0)	// rayData
			.AddStorageImage(3)		// 辐照度图集（读写）
			.AddUnifromBuffer(4)	// DDGIUniforms
			.AddStorageImage(5)		// 深度图集（读写）
			.AddStorageImage(8)		// probeData（读写）
			.AddStorageImage(9)		// probeStats（读写）
			.AddStorageBuffer(10)	// 体积表
			.AddStorageBuffer(11)	// probeInfo
			.AddStorageBuffer(12);	// 更新列表

		if (config.Validate())
			_shader.Create(config);
	}
	{
		ComputePipelineConfig config;
		config.computePath = bakeCopyPath;
		DDGIAddLayoutDefines(config);
		config
			.AddUnifromBuffer(4)	// DDGIUniforms
			.AddStorageImage(8)		// probeData（只读）
			.AddStorageBuffer(10)	// 体积表
			.AddStorageBuffer(13);	// 烘焙回读

		if (config.Validate())
			_bakeShader.Create(config);
	}
}

DDGIBlendPass::~DDGIBlendPass()
{}

bool DDGIBlendPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable && _sharedState != nullptr;
}

void DDGIBlendPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto* framePtr = registry.TryLoad<DDGIFramePtr>(kFrameKey);
	if (!framePtr || !*framePtr)
		return;
	const DDGIFramePtr frameHolder = *framePtr;
	const DDGIFrame& frame = *frameHolder;
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!frame.enabled)
		return;

	const bool bake = frame.bakeActive && frame.bakeVolumeIndex < frame.volumes.size();
	if (frame.updateCount == 0 && !bake)
		return;

	auto rayData = ctx.GetInput(0);
	auto irradianceAtlas = ctx.GetPersitent(0);
	auto depthAtlas = ctx.GetPersitent(1);
	auto probeData = ctx.GetPersitent(2);
	auto probeStats = ctx.GetPersitent(3);

	for (auto* tex : { &irradianceAtlas, &depthAtlas, &probeData, &probeStats })
	{
		if (!*tex || (*tex)->IsEmpty())
			return;
	}

	auto cmd = cmdCtx.GetCmd();
	auto ubo = frame.uniformBlock;

	irradianceAtlas->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	depthAtlas->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	probeData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	probeStats->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	if (frame.updateCount > 0 && rayData && !rayData->IsEmpty())
	{
		rayData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

		ComputeBindingRecord binding;
		binding.SetUniformTexture(rayData, vk::ImageAspectFlagBits::eColor, 0);
		binding.SetStorageImage(irradianceAtlas, vk::ImageAspectFlagBits::eColor, 3);
		binding.SetUniformBlock(ubo, 4);
		binding.SetStorageImage(depthAtlas, vk::ImageAspectFlagBits::eColor, 5);
		binding.SetStorageImage(probeData, vk::ImageAspectFlagBits::eColor, 8);
		binding.SetStorageImage(probeStats, vk::ImageAspectFlagBits::eColor, 9);
		binding.SetStorageBlock(frame.volumeTable, 10);
		binding.SetStorageBlock(frame.probeInfo, 11);
		binding.SetStorageBlock(frame.updateList, 12);

		_shader.Bind(cmd, binding);
		// 一个 workgroup 一个探针（预算上限 65535，不超过 x 维派发上限）
		cmd->dispatch(frame.updateCount, 1, 1);
	}

	if (!bake)
	{
		cmd->SubmitToQueue();
		return;
	}

	// 烘焙是编辑器里的一次性操作：混合写完后把该体积的 probeData 拷进回读缓冲，直接同步读回
	const auto& v = frame.volumes[frame.bakeVolumeIndex];
	const uint32_t bakeCount = v.probeCount;
	auto bakeBuffer = std::make_shared<StorageBlock>(uint64_t(bakeCount) * sizeof(glm::vec4));

	probeData->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord bakeBinding;
	bakeBinding.SetUniformBlock(ubo, 4);
	bakeBinding.SetStorageImage(probeData, vk::ImageAspectFlagBits::eColor, 8);
	bakeBinding.SetStorageBlock(frame.volumeTable, 10);
	bakeBinding.SetStorageBlock(bakeBuffer, 13);
	_bakeShader.Bind(cmd, bakeBinding);
	cmd->dispatch((bakeCount + 63) / 64, 1, 1);

	bakeBuffer->Barrier(cmd, BufferUsage::StorageWrite, BufferUsage::TransferRead);
	DDGIBakeResult result;
	result.requestId = frame.bakeRequestId;
	result.volumeId = v.id;
	result.originCell = v.originCell;
	result.counts = v.counts;
	result.spacing = v.spacing;
	result.probes.resize(bakeCount);
	if (!bakeBuffer->Readback(cmd, result.probes.data(), result.probes.size() * sizeof(glm::vec4)))
		result.probes.clear();
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (auto& channel = _sharedState->GetDebugChannel())
		channel->PublishBake(std::move(result));
}

void DDGIBlendPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;
	registry.Store(kFrameKey, _sharedState->Resolve(state));
}

void DDGIBlendPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}
