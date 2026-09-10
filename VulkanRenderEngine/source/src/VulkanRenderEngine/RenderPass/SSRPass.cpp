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

SSRPass::SSRPass(
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
			.AddUnifromTexture(8);

		if (config.Validate())
			_ssrShader.Create(config);
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

	_SSRParamsUBO = std::make_shared<UniformBlock>(sizeof(SSRParams));
	_SpatialDenoisingParamsUBO = std::make_shared<UniformBlock>(sizeof(SpatialDenoisingParams));
	_TemporalAccumulateParamsUBO = std::make_shared<UniformBlock>(sizeof(TemporalAccumulateParams));

	_ssrShader.SetUniformBlock(_SSRParamsUBO, 0);
	_spatialDenoisingShader.SetUniformBlock(_SpatialDenoisingParamsUBO, 0);
	_temporalDenoisingShader.SetUniformBlock(_TemporalAccumulateParamsUBO, 0);
}

bool SSRPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssrTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSRPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
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
	data.gMotionVector = ctx.GetInput(4);
	data.hzbDepthMap = ctx.GetInput(5);

	data.colorMap = ctx.GetExternal(0);
	data.depthMap = ctx.GetExternal(1);

	if (!DrawSSR(data, state)) return;
	if (!DrawSpatialDenoising(data, state)) return;
	if (!DrawTemporalDenoising(data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
		Texture2D::CopyTexture(data.outPutTexture, data.historyColorTexture);
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

	{

		SpatialDenoisingParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.kernelSize = state.option.ssrTraceParams.BlurKernelSize,
			.sigma = state.option.ssrTraceParams.BlurGaussSigma,
			.blurRadius = state.option.ssrTraceParams.BlurRadius,
			.blurDepthWeight = state.option.ssrTraceParams.BlurDepthWeight
		};
		_SpatialDenoisingParamsUBO->WriteData(&params, sizeof(SpatialDenoisingParams));
	}

	{
		TemporalAccumulateParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.initBlendFactor = state.option.ssrTraceParams.initBlendFactor,
			.dynamicBlendFactor = state.option.ssrTraceParams.dynamicBlendFactor
		};
		_TemporalAccumulateParamsUBO->WriteData(&params, sizeof(TemporalAccumulateParams));
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

bool SSRPass::DrawSSR(FrameRenderData& data, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	target->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Output);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	_ssrShader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_ssrShader.SetStorageImage(target, 1);
	_ssrShader.SetUniformTexture(data.gPosition, 2);
	_ssrShader.SetUniformTexture(data.gNormal, 3);
	_ssrShader.SetUniformTexture(data.gAlbedoOpacity, 4);
	_ssrShader.SetUniformTexture(data.gMetallicRoughness, 5);
	_ssrShader.SetUniformTexture(data.colorMap, 6);
	_ssrShader.SetUniformTexture(data.depthMap, 7);
	_ssrShader.SetUniformTexture(data.hzbDepthMap, 8);

	_ssrShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

bool SSRPass::DrawSpatialDenoising(FrameRenderData& data, RenderState& state)
{
	auto& source = data.originTexture;
	auto& target = data.spatialDenoisingTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	target->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Output);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	_spatialDenoisingShader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_spatialDenoisingShader.SetStorageImage(target, 1);
	_spatialDenoisingShader.SetUniformTexture(data.gNormal, 2);
	_spatialDenoisingShader.SetUniformTexture(data.depthMap, 3);
	_spatialDenoisingShader.SetUniformTexture(source, 4);

	_spatialDenoisingShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

bool SSRPass::DrawTemporalDenoising(FrameRenderData& data, RenderState& state)
{
	auto& source = data.spatialDenoisingTexture;
	auto& target = data.outPutTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	if (_firstDrawTemporal || !data.gMotionVector || !data.historyColorTexture)
	{
		Texture2D::CopyTexture(source, target);
		_firstDrawTemporal = false;
		return true;
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();

	target->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Output);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	_temporalDenoisingShader.SetStorageImage(target, 1);
	_temporalDenoisingShader.SetUniformTexture(source, 2);
	_temporalDenoisingShader.SetUniformTexture(data.historyColorTexture, 3);
	_temporalDenoisingShader.SetUniformTexture(data.gMotionVector, 4);

	_temporalDenoisingShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}
