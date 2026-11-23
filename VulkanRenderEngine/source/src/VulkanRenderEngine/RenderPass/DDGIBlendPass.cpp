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
		.AddUnifromTexture(6)	// probeData history
		.AddUnifromTexture(7)	// probeStats history
		.AddStorageImage(8)		// probeData
		.AddStorageImage(9)		// probeStats
		.AddStorageBuffer(10)	// 体积表
		.AddStorageBuffer(11)	// probeInfo
		.AddStorageBuffer(12)	// 槽位 -> rayData tile
		.AddStorageBuffer(13);	// 烘焙回读

	if (config.Validate())
		_shader.Create(config);

	_dummyBake = std::make_shared<StorageBlock>(16);
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

	auto rayData = ctx.GetInput(0);
	auto irradianceHistory = ctx.GetPersitent(0);
	auto depthHistory = ctx.GetPersitent(1);
	auto probeDataHistory = ctx.GetPersitent(2);
	auto probeStatsHistory = ctx.GetPersitent(3);
	auto irradianceOut = ctx.GetOutput(0);
	auto depthOut = ctx.GetOutput(1);
	auto probeDataOut = ctx.GetOutput(2);
	auto probeStatsOut = ctx.GetOutput(3);

	for (auto* tex : { &rayData, &irradianceHistory, &depthHistory, &probeDataHistory, &probeStatsHistory,
		&irradianceOut, &depthOut, &probeDataOut, &probeStatsOut })
	{
		if (!*tex || (*tex)->IsEmpty())
			return;
	}

	auto cmd = cmdCtx.GetCmd();

	auto ubo = frame.uniformBlock;

	// 烘焙：回读该体积每个探针的 (偏移, n)
	std::shared_ptr<StorageBlock> bakeBuffer = _dummyBake;
	uint32_t bakeCount = 0;
	if (frame.bakeActive && frame.bakeVolumeIndex < frame.volumes.size())
	{
		bakeCount = frame.volumes[frame.bakeVolumeIndex].probeCount;
		bakeBuffer = std::make_shared<StorageBlock>(uint64_t(bakeCount) * sizeof(glm::vec4));
	}

	rayData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	irradianceHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	depthHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	probeDataHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	probeStatsHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	irradianceOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	depthOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	probeDataOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	probeStatsOut->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetUniformTexture(rayData, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(irradianceHistory, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(depthHistory, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetStorageImage(irradianceOut, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformBlock(ubo, 4);
	binding.SetStorageImage(depthOut, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(probeDataHistory, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(probeStatsHistory, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetStorageImage(probeDataOut, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetStorageImage(probeStatsOut, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetStorageBlock(frame.volumeTable, 10);
	binding.SetStorageBlock(frame.probeInfo, 11);
	binding.SetStorageBlock(frame.slotToRay, 12);
	binding.SetStorageBlock(bakeBuffer, 13);

	_shader.Bind(cmd, binding);

	// 二维派发：z 维上限只有 65535，探针池可能更大
	const uint32_t perRow = DDGIAtlasLayout::Get().probesPerRow;
	const uint32_t rows = (frame.totalProbes + perRow - 1) / perRow;
	cmd->dispatch(std::min(perRow, frame.totalProbes), rows, 1);

	// 回拷 history，供下一帧混合、多次反弹与偏移。不能让同一张图同帧既读又写。
	// 只拷本帧用到的行：槽位按 (slot % perRow, slot / perRow) 排，本帧只用了 [0, totalProbes)；
	// 下一帧按本帧布局（prevBase / prevCounts）读 history，也都落在这个范围里，后面的行没人读。
	// 图集按容量（默认 65536 个探针）分配，全量拷贝每帧约 226 MB 读写，和实际探针数无关。
	{
		const uint32_t usedCols = std::min(perRow, frame.totalProbes);
		auto copyUsed = [&](const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dst, uint32_t tileSize) {
			Texture2D::CopyTextureRegionAsync(cmd, src, dst, usedCols * tileSize, rows * tileSize);
			};
		copyUsed(irradianceOut, irradianceHistory, DDGIAtlasLayout::IrradianceTileSize);
		copyUsed(depthOut, depthHistory, DDGIAtlasLayout::DepthTileSize);
		copyUsed(probeDataOut, probeDataHistory, 1u);
		copyUsed(probeStatsOut, probeStatsHistory, 1u);
	}

	if (bakeCount == 0)
	{
		cmd->SubmitToQueue();
		return;
	}

	// 烘焙是编辑器里的一次性操作，这里直接同步读回
	bakeBuffer->Barrier(cmd, BufferUsage::StorageWrite, BufferUsage::TransferRead);
	DDGIBakeResult result;
	const auto& v = frame.volumes[frame.bakeVolumeIndex];
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
