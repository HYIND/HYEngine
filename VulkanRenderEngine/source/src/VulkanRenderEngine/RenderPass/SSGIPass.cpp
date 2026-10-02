#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSGIPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"





struct SSGIParams
{
	glm::ivec2 screenSize;
	float tMin;
	float tMax;
	uint32_t maxBounce = 0;
	uint32_t SampleRayCount;
	uint32_t RayMarchingMaxStep;
	float SampleIndirectClampValue;
	float GIIntensity;
	float AOIntensity;
	float DistanceFactor;
	uint32_t frameIndex;
};


SSGIPass::SSGIPass(
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
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
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
			.AddUnifromTexture(8)
			.AddUnifromTexture(9);

		if (config.Validate())
			_ssgiShader.Create(config);
	}

	_SSGIParamsUBO = std::make_shared<UniformBlock>(sizeof(SSGIParams));

	_ssgiShaderBinding.SetUniformBlock(_SSGIParamsUBO, 0);
}

bool SSGIPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssgiTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSGIPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
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
	data.ssaoTexture = ctx.GetInput(11);

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

	if (!DrawSSGI(cmd, data, state)) return;

	if (!DrawTemporalAccumulate(cmd, data, state)) return;

	if (data.temporalAccumulateColorTexture && data.temporalAccumulateHistoryColorTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateColorTexture, data.temporalAccumulateHistoryColorTexture);
	if (data.temporalAccumulateMomentTexture && data.temporalAccumulateHistoryMomentTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateMomentTexture, data.temporalAccumulateHistoryMomentTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (!DrawSpatialDenoising(cmd, data, state)) return;

}

void SSGIPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.ssgiOn);

	if (!ShouldExecute(registry, state))
		return;

	{

		SSGIParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.tMin = std::max(0.f, state.option.ssgiTraceParams.tMin),
			.tMax = std::max(0.f, state.option.ssgiTraceParams.tMax),
			.maxBounce = std::max(1u, std::min(state.option.ssgiTraceParams.maxBounceLimit, GlobalConfig::SSTrace_Max_Bounce_limit)),
			.SampleRayCount = state.option.ssgiTraceParams.NumSamples,
			.RayMarchingMaxStep = std::max(2u, state.option.ssgiTraceParams.RayMarchingMaxStep),
			.SampleIndirectClampValue = std::max(0.01f, state.option.ssgiTraceParams.Sample_Indirect_Clamp_Value),
			.GIIntensity = std::max(0.01f, state.option.ssgiTraceParams.GIIntensity),
			.AOIntensity = std::max(0.01f, state.option.ssgiTraceParams.AOIntensity),
			.DistanceFactor = std::max(0.0001f, state.option.ssgiTraceParams.DistanceFactor),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_SSGIParamsUBO->WriteData(&params, sizeof(SSGIParams));
	}
}

void SSGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool SSGIPass::DrawSSGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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

	_ssgiShaderBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_ssgiShaderBinding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 1);
	_ssgiShaderBinding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 2);
	_ssgiShaderBinding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 3);
	_ssgiShaderBinding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 4);
	_ssgiShaderBinding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 5);
	_ssgiShaderBinding.SetUniformTexture(data.sceneColorMap, vk::ImageAspectFlagBits::eColor, 6);
	_ssgiShaderBinding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 7);
	_ssgiShaderBinding.SetUniformTexture(data.ssaoTexture, vk::ImageAspectFlagBits::eColor, 8);
	_ssgiShaderBinding.SetUniformTexture(data.hzbDepthMap, vk::ImageAspectFlagBits::eDepth, 9);

	_ssgiShader.Bind(cmd, _ssgiShaderBinding);
	cmd->dispatch((data.drawSize.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (data.drawSize.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSGIPass::DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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
		.maxAccumulateCount = state.option.ssgiTraceParams.maxAccumulateCount
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

bool SSGIPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.temporalAccumulateColorTexture;
	auto& target = data.outPutTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	AtrousBilateralFilter::Params params{
		.normalFactor = state.option.ssgiTraceParams.normalFactor,
		.depthFactor = state.option.ssgiTraceParams.depthFactor,
		.luminanceFactor = state.option.ssgiTraceParams.luminanceFactor
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
		state.option.ssgiTraceParams.filterCount
	);

	cmd->SubmitToQueue();

	return true;
}