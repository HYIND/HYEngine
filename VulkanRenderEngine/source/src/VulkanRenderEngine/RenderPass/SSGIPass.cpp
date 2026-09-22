#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SSGIPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;


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

struct SpatialDenoisingParams
{
	glm::ivec2 screenSize;
	uint32_t kernelSize;
	float sigma;
	float blurRadius;
	float blurDepthWeight;
};

struct TemporalAccumulateParams
{
	glm::ivec2 screenSize;
	float initBlendFactor;
	float dynamicBlendFactor;
};

SSGIPass::SSGIPass(
	const std::string& computerShaderPath,
	const std::string& spatialDenoisingComputerShaderPath,
	const std::string& temporalDenoisingComputerShaderPath
)
	:
	_enable(false)
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
			.AddUnifromTexture(8)
			.AddUnifromTexture(9);

		if (config.Validate())
			_ssgiShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = spatialDenoisingComputerShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4);

		if (config.Validate())
			_spatialDenoisingShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = temporalDenoisingComputerShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2)
			.AddUnifromTexture(3)
			.AddUnifromTexture(4);

		if (config.Validate())
			_temporalDenoisingShader.Create(config);
	}

	_SSGIParamsUBO = std::make_shared<UniformBlock>(sizeof(SSGIParams));
	_SpatialDenoisingParamsUBO = std::make_shared<UniformBlock>(sizeof(SpatialDenoisingParams));
	_TemporalAccumulateParamsUBO = std::make_shared<UniformBlock>(sizeof(TemporalAccumulateParams));

	_ssgiShaderBinding.SetUniformBlock(_SSGIParamsUBO, 0);
	_spatialDenoisingShaderBinding.SetUniformBlock(_SpatialDenoisingParamsUBO, 0);
	_temporalDenoisingShaderBinding.SetUniformBlock(_TemporalAccumulateParamsUBO, 0);
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
	data.originTexture = ctx.GetTemp(0);
	data.spatialDenoisingTexture = ctx.GetTemp(1);

	data.outPutTexture = ctx.GetOutput(0);

	data.historyColorTexture = ctx.GetPersitent(0);

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.ssaoTexture = ctx.GetInput(4);
	data.gMotionVector = ctx.GetInput(5);
	data.hzbDepthMap = ctx.GetInput(6);

	data.colorMap = ctx.GetExternal(0);
	data.depthMap = ctx.GetExternal(1);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawSSGI(cmd, data, state)) return;
	if (!DrawSpatialDenoising(cmd, data, state)) return;
	if (!DrawTemporalDenoising(cmd, data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
	{
		Texture2D::CopyTextureAsync(cmd, data.outPutTexture, data.historyColorTexture);
		cmd->SubmitToQueue();
	}
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

	{

		SpatialDenoisingParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.kernelSize = state.option.ssgiTraceParams.BlurKernelSize,
			.sigma = state.option.ssgiTraceParams.BlurGaussSigma,
			.blurRadius = state.option.ssgiTraceParams.BlurRadius,
			.blurDepthWeight = state.option.ssgiTraceParams.BlurDepthWeight
		};
		_SpatialDenoisingParamsUBO->WriteData(&params, sizeof(SpatialDenoisingParams));
	}

	{
		TemporalAccumulateParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.initBlendFactor = state.option.ssgiTraceParams.initBlendFactor,
			.dynamicBlendFactor = state.option.ssgiTraceParams.dynamicBlendFactor
		};
		_TemporalAccumulateParamsUBO->WriteData(&params, sizeof(TemporalAccumulateParams));
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
	_ssgiShaderBinding.SetUniformTexture(data.colorMap, vk::ImageAspectFlagBits::eColor, 6);
	_ssgiShaderBinding.SetUniformTexture(data.depthMap, vk::ImageAspectFlagBits::eDepth, 7);
	_ssgiShaderBinding.SetUniformTexture(data.ssaoTexture, vk::ImageAspectFlagBits::eColor, 8);
	_ssgiShaderBinding.SetUniformTexture(data.hzbDepthMap, vk::ImageAspectFlagBits::eDepth, 9);

	_ssgiShader.Bind(cmd, _ssgiShaderBinding);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSGIPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.originTexture;
	auto& target = data.spatialDenoisingTexture;

	if (!source || source->IsEmpty())
		return false;

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

	_spatialDenoisingShaderBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_spatialDenoisingShaderBinding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 1);
	_spatialDenoisingShaderBinding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 2);
	_spatialDenoisingShaderBinding.SetUniformTexture(data.depthMap, vk::ImageAspectFlagBits::eDepth, 3);
	_spatialDenoisingShaderBinding.SetUniformTexture(source, vk::ImageAspectFlagBits::eColor, 4);

	_spatialDenoisingShader.Bind(cmd, _spatialDenoisingShaderBinding);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool SSGIPass::DrawTemporalDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.spatialDenoisingTexture;
	auto& target = data.outPutTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	if (_firstDrawTemporal || !data.gMotionVector || !data.historyColorTexture)
	{
		Texture2D::CopyTextureAsync(cmd, source, target);
		_firstDrawTemporal = false;
		cmd->SubmitToQueue();
		return true;
	}

	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	_temporalDenoisingShaderBinding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 1);
	_temporalDenoisingShaderBinding.SetUniformTexture(source, vk::ImageAspectFlagBits::eColor, 2);
	_temporalDenoisingShaderBinding.SetUniformTexture(data.historyColorTexture, vk::ImageAspectFlagBits::eColor, 3);
	_temporalDenoisingShaderBinding.SetUniformTexture(data.gMotionVector, vk::ImageAspectFlagBits::eColor, 4);

	_temporalDenoisingShader.Bind(cmd, _temporalDenoisingShaderBinding);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();

	return true;
}
