#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSRPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;


struct SSRParams
{
	glm::ivec2 screenSize;
	float tMin;
	float tMax;
	uint32_t maxBounce = 0;
	uint32_t SampleRayCount;
	uint32_t RayMarchingMaxStep;
	float SampleIndirectClampValue;
	float DistanceFactor;
	uint32_t frameIndex;
};


SSRPass::SSRPass(
	const std::string& computerShaderPath,
	const std::string& atrousComputerShaderPath,
	const std::string& temporalAccumulateComputerShaderPath
)
	:
	_firstDrawTemporal(true),
	_enable(false),
	_temporalAccumulate(temporalAccumulateComputerShaderPath),
	_spatialDenoisingFilter(atrousComputerShaderPath)
{

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.AddDefineMacro("Max_Bounce_limit", GlobalConfig::SSTrace_Max_Bounce_limit);
		config.computePath = computerShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4)
			.AddUnifromTexture(5)
			.AddUnifromTexture(6)
			.AddUnifromTexture(7)
			.AddUnifromTexture(8);

		if (config.Validate())
			_ssrShader.Create(config);
	}


	_SSRParamsUBO = std::make_shared<UniformBlock>(sizeof(SSRParams));

	_ssrShaderBinding.SetUniformBlock(_SSRParamsUBO, 0);
}

bool SSRPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssrTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSRPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.atlasShadowMap = ctx.GetInput(4);
	data.ssaoMap = ctx.GetInput(5);
	data.gMotionVector = ctx.GetInput(6);
	data.gPrevPosition = ctx.GetInput(7);
	data.gPrevNormal = ctx.GetInput(8);
	data.gPrevDepthStencil = ctx.GetInput(9);
	data.hzbDepthMap = ctx.GetInput(10);

	data.gDepthStencil = ctx.GetFrameLocal(0);

	data.originTexture = ctx.GetTemp(0);
	data.temporalAccumulateColorTexture = ctx.GetTemp(1);
	data.temporalAccumulateMomentTexture = ctx.GetTemp(2);
	data.spatialDenoisingTempTexture = ctx.GetTemp(3);

	data.temporalAccumulateHistoryColorTexture = ctx.GetPersitent(0);
	data.temporalAccumulateHistoryMomentTexture = ctx.GetPersitent(1);

	data.outPutTexture = ctx.GetOutput(0);

	data.sceneColorMap = ctx.GetExternal(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawSSR(cmd, data, state)) return;

	if (!DrawTemporalAccumulate(cmd, data, state)) return;

	if (data.temporalAccumulateColorTexture && data.temporalAccumulateHistoryColorTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateColorTexture, data.temporalAccumulateHistoryColorTexture);
	if (data.temporalAccumulateMomentTexture && data.temporalAccumulateHistoryMomentTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateMomentTexture, data.temporalAccumulateHistoryMomentTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (!DrawSpatialDenoising(cmd, data, state)) return;

}

void SSRPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.ssrOn);

	if (!ShouldExecute(registry, state))
		return;

	{

		SSRParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.tMin = std::max(0.f, state.option.ssrTraceParams.tMin),
			.tMax = std::max(0.f, state.option.ssrTraceParams.tMax),
			.maxBounce = std::max(1u, std::min(state.option.ssrTraceParams.maxBounceLimit, GlobalConfig::SSTrace_Max_Bounce_limit)),
			.SampleRayCount = state.option.ssrTraceParams.NumSamples,
			.RayMarchingMaxStep = std::max(2u, state.option.ssrTraceParams.RayMarchingMaxStep),
			.SampleIndirectClampValue = std::max(0.01f, state.option.ssrTraceParams.Sample_Indirect_Clamp_Value),
			.DistanceFactor = std::max(0.0001f, state.option.ssrTraceParams.DistanceFactor),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_SSRParamsUBO->WriteData(&params, sizeof(SSRParams));
	}
}

void SSRPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool SSRPass::DrawSSR(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;


	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	_ssrShaderBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_ssrShaderBinding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 1);
	_ssrShaderBinding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 2);
	_ssrShaderBinding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 3);
	_ssrShaderBinding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 4);
	_ssrShaderBinding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 5);
	_ssrShaderBinding.SetUniformTexture(data.sceneColorMap, vk::ImageAspectFlagBits::eColor, 6);
	_ssrShaderBinding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 7);
	_ssrShaderBinding.SetUniformTexture(data.hzbDepthMap, vk::ImageAspectFlagBits::eDepth, 8);

	_ssrShader.Bind(cmd, _ssrShaderBinding);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSRPass::DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.originTexture;
	auto& target = data.temporalAccumulateColorTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source
		|| source->IsEmpty()
		|| !target
		|| target->IsEmpty()
		|| !moment
		|| moment->IsEmpty()
		)
		return false;

	if (_firstDrawTemporal)
	{
		vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		vk::ClearColorValue clearMoments = { 0.0f, 0.0f, 0.0f, 0.0f };
		vk::ImageSubresourceRange range;
		range.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
			.setBaseMipLevel(0)
			.setLevelCount(1);
		cmd->clearColorImage(data.temporalAccumulateHistoryColorTexture->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
		cmd->clearColorImage(data.temporalAccumulateHistoryMomentTexture->GetImage(), vk::ImageLayout::eGeneral, clearMoments, range);
		_firstDrawTemporal = false;
	}

	TemporalAccumulate::Params params{
		.maxAccumulateCount = state.option.ssrTraceParams.maxAccumulateCount
	};

	_temporalAccumulate.Execute(
		cmd,
		source,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gMotionVector,
		data.gPrevPosition,
		data.gPrevNormal,
		data.gPrevDepthStencil,
		target,
		moment,
		data.temporalAccumulateHistoryColorTexture,
		data.temporalAccumulateHistoryMomentTexture,
		params,
		state.camera.curUBO,
		state.camera.prevUBO
	);

	cmd->SubmitToQueue();

	return true;
}

bool SSRPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.temporalAccumulateColorTexture;
	auto& target = data.outPutTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	AtrousBilateralFilter::Params params{
		.normalFactor = state.option.ssrTraceParams.normalFactor,
		.depthFactor = state.option.ssrTraceParams.depthFactor,
		.luminanceFactor = state.option.ssrTraceParams.luminanceFactor
	};

	_spatialDenoisingFilter.Execute(
		cmd,
		source,
		data.gNormal,
		data.gDepthStencil,
		moment,
		target,
		data.spatialDenoisingTempTexture,
		params,
		state.camera.curUBO,
		state.camera.prevUBO,
		state.option.ssrTraceParams.filterCount
	);

	cmd->SubmitToQueue();

	return true;
}