#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIApplyPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/General/IndirectLightingPolicy.h"

namespace
{
	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kFrameKey = "ddgiApplyFrame";

	// 与 DDGIApply.comp 的 DDGIApplyParams 对应
	struct DDGIApplyParams
	{
		glm::uvec4 flags = glm::uvec4(0u);	// x = IBL 漫反射回退开关
	};
}

DDGIApplyPass::DDGIApplyPass(const std::string& computePath)
{
	// 两个变体：平常用的不带任何调试分支；漏光调试打开时换成带记录的那个
	auto create = [&](ComputePipeline& shader, bool leakDebug) {
		ComputePipelineConfig config;
		config.computePath = computePath;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		DDGIAddLayoutDefines(config);
		if (leakDebug)
			config.AddDefineMacro("DDGI_LEAK_ENABLE");
		config
			.AddStorageImage(0)			// 输出
			.AddUnifromTexture(1)		// gPosition
			.AddUnifromTexture(2)		// gNormal
			.AddUnifromTexture(3)		// gAlbedoOpacity
			.AddUnifromTexture(4)		// SSAO
			.AddUnifromTexture(5)		// 深度
			.AddUnifromTexture(6)		// 辐照度图集
			.AddUnifromTexture(7)		// 深度图集
			.AddUnifromBuffer(8)		// DDGIUniforms
			.AddUnifromTexture(9)		// probeData
			.AddUnifromTexture(10)		// gMetallicRoughness
			.AddStorageBuffer(11)		// 体积表
			.AddStorageBuffer(12)		// 漏光调试记录（只有调试变体真正用到）
			.AddUnifromTexture(13)		// IBL 漫反射 cube（回退用）
			.AddUnifromBuffer(14);		// DDGIApplyParams

		if (config.Validate())
			shader.Create(config);
		};
	create(_shader, false);
	create(_leakShader, true);

	_leakBuffer = std::make_shared<StorageBlock>(sizeof(DDGILeakRecordGPU));
}

DDGIApplyPass::~DDGIApplyPass()
{}

bool DDGIApplyPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable && _sharedState != nullptr;
}

void DDGIApplyPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto* framePtr = registry.TryLoad<DDGIFramePtr>(kFrameKey);
	if (!framePtr || !*framePtr)
		return;
	const DDGIFramePtr frameHolder = *framePtr;
	const DDGIFrame& frame = *frameHolder;
	registry.Store(kFrameKey, DDGIFramePtr{});

	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto ssaoMap = ctx.GetInput(3);
	auto gMetallicRoughness = ctx.GetInput(4);
	// DDGI 图集是持久资源（原地更新），顺序由 .After(ddgiBlendNode) 保证
	auto irradianceAtlas = ctx.GetPersitent(0);
	auto depthAtlas = ctx.GetPersitent(1);
	auto probeData = ctx.GetPersitent(2);
	auto gDepthStencil = ctx.GetFrameLocal(0);
	auto outImage = ctx.GetOutput(0);

	for (auto* tex : { &gPosition, &gNormal, &gAlbedoOpacity, &ssaoMap, &irradianceAtlas, &depthAtlas,
		&probeData, &gMetallicRoughness, &gDepthStencil, &outImage })
	{
		if (!*tex || (*tex)->IsEmpty())
			return;
	}

	auto cmd = cmdCtx.GetCmd();
	outImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	// 没有可用体积时 uniforms.counts.x = 0，输出清零（combin 是加法合成）
	std::shared_ptr<UniformBlock> ubo = frame.uniformBlock;
	if (!ubo)
	{
		DDGIUniforms empty{};
		ubo = DDGIMakeHostBlock<UniformBlock>(&empty, sizeof(empty));
	}

	const bool leak = frame.enabled && frame.uniforms.flags.w != 0u;
	ComputePipeline& shader = leak ? _leakShader : _shader;

	// IBL 漫反射回退：DDGI 没覆盖到的权重交给 IBL 漫反射。
	// cube 与 LightingPass 取自同一份（SelectIBLResources），保证体积外的结果和不开 DDGI 时一致。
	const IBLResources ibl = SelectIBLResources(state);
	const std::shared_ptr<TextureCube> iblDiffuse = ibl.IBLReady() ? ibl.diffuse : nullptr;

	DDGIApplyParams applyParams;
	applyParams.flags.x = iblDiffuse ? 1u : 0u;
	auto applyParamsBlock = DDGIMakeHostBlock<UniformBlock>(&applyParams, sizeof(applyParams));

	ComputeBindingRecord binding;
	binding.SetStorageImage(outImage, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(gPosition, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(gNormal, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(ssaoMap, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(gDepthStencil, vk::ImageAspectFlagBits::eDepth, 5);
	binding.SetUniformTexture(irradianceAtlas, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(depthAtlas, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformBlock(ubo, 8);
	binding.SetUniformTexture(probeData, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetStorageBlock(frame.volumeTable ? frame.volumeTable : _leakBuffer, 11);
	binding.SetStorageBlock(_leakBuffer, 12);
	binding.SetUniformBlock(applyParamsBlock, 14);
	if (iblDiffuse)
		binding.SetUniformTextureCube(iblDiffuse, vk::ImageAspectFlagBits::eColor, 13);

	shader.Bind(cmd, binding);

	auto size = outImage->GetSize();
	cmd->dispatch(
		(size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1
	);

	if (!leak)
	{
		cmd->SubmitToQueue();
		return;
	}

	// 漏光调试：只在编辑器打开时才走，直接同步读回
	_leakBuffer->Barrier(cmd, BufferUsage::StorageWrite, BufferUsage::TransferRead);
	DDGILeakRecordGPU record{};
	const bool ok = _leakBuffer->Readback(cmd, &record, sizeof(record));
	if (cmd->IsRecording())
		cmd->SubmitToQueue();
	if (ok)
		PublishLeak(frame, record);
}

void DDGIApplyPass::PublishLeak(const DDGIFrame& frame, const DDGILeakRecordGPU& record)
{
	auto& channel = _sharedState->GetDebugChannel();
	if (!channel)
		return;

	DDGILeakResult result;
	result.valid = record.header.x != 0u;
	result.frameIndex = frame.frameIndex;
	result.worldPos = glm::vec3(record.worldPos);
	result.normal = glm::vec3(record.normal);
	result.biasedPos = glm::vec3(record.biasedPos);
	result.irradiance = glm::vec3(record.result);
	result.volumeCount = std::min(record.header.z, DDGIAtlasLayout::LeakMaxVolumes);
	for (uint32_t i = 0; i < result.volumeCount; ++i)
	{
		result.volumeIds[i] = record.volumeIds[i];
		result.volumeWeights[i] = record.volumeWeights[i];
	}

	const uint32_t probeCount = std::min(record.header.y, DDGIAtlasLayout::LeakMaxProbes);
	result.probes.reserve(probeCount);
	for (uint32_t i = 0; i < probeCount; ++i)
	{
		const auto& p = record.probes[i];
		DDGILeakProbe probe;
		probe.position = glm::vec3(p.position);
		probe.volumeIndex = uint32_t(std::max(0.0f, p.position.w));
		probe.cell = glm::ivec3(p.cell);
		probe.slot = uint32_t(p.cell.w);
		probe.wTrilinear = p.weights.x;
		probe.wBackface = p.weights.y;
		probe.wVisibility = p.weights.z;
		probe.wFinal = p.weights.w;
		probe.meanDistance = p.extra.x;
		probe.distance = p.extra.y;
		probe.interior = p.extra.z;
		probe.valid = p.extra.w > 0.5f;
		result.probes.push_back(probe);
	}

	channel->PublishLeak(std::move(result));
}

void DDGIApplyPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;
	registry.Store(kFrameKey, _sharedState->Resolve(state));
}

void DDGIApplyPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}
