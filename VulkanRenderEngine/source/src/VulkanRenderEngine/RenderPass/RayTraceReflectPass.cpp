#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RayTraceReflectPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"


struct CubeParams {
	uint32_t CubeEnable = 0;
	uint32_t IBLEnable = 0;
};

struct RayTraceParams
{
	glm::ivec2 screenSize;
	float tMin = 0.f;
	float tMax = 0.f;
	uint32_t maxBounce = 0;
	uint32_t sampleRayCount = 0;
	uint32_t frameIndex;
};


RayTraceReflectPass::RayTraceReflectPass(
	const std::string& rayTraceComputerShaderPath,
	const std::string& atrousComputerShaderPath,
	const std::string& temporalAccumulateComputerShaderPath,
	const std::string& scaleComputerShaderPath
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
		config.AddDefineMacro("Max_Recursive_Depth", GlobalConfig::RayTrace_Max_Recursive_Depth);
		config.AddDefineMacro("Max_Bounce_limit", GlobalConfig::RayTrace_Max_Bounce_limit);
		config.computePath = rayTraceComputerShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddBindlessMaterialTextureBinding()
			.AddLightDataBinding()
			.AddStorageBuffer(0)
			.AddStorageBuffer(1)
			.AddStorageBuffer(2)
			.AddStorageBuffer(3)
			.AddStorageBuffer(4)
			.AddUnifromBuffer(5)
			.AddUnifromBuffer(6)
			.AddStorageImage(7)
			.AddUnifromTexture(8)
			.AddUnifromTexture(9)
			.AddUnifromTexture(10)
			.AddUnifromTexture(11)
			.AddUnifromTexture(12)
			.AddUnifromTexture(13)
			.AddUnifromTexture(14)
			.AddUnifromTexture(15)
			.AddUnifromTexture(16)
			.AddUnifromTexture(17)
			.AddUnifromTexture(18);

		if (config.Validate())
			_rayTraceShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = scaleComputerShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddStorageImage(2);

		if (config.Validate())
			_scaleShader.Create(config);
	}

	_RayTraceParamsUBO = std::make_shared<UniformBlock>(sizeof(RayTraceParams));
}

RayTraceReflectPass::~RayTraceReflectPass()
{}

bool RayTraceReflectPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.rayTraceReflectParams.maxBounceLimit < 0)
		return false;
	return true;
}

void RayTraceReflectPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
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

	data.gDepthStencil = ctx.GetFrameLocal(0);

	data.originTexture = ctx.GetTemp(0);
	data.temporalAccumulateColorTexture = ctx.GetTemp(1);
	data.temporalAccumulateMomentTexture = ctx.GetTemp(2);
	data.spatialDenoisingTempTexture = ctx.GetTemp(3);

	data.temporalAccumulateHistoryColorTexture = ctx.GetPersitent(0);
	data.temporalAccumulateHistoryMomentTexture = ctx.GetPersitent(1);

	data.outPutTexture = ctx.GetOutput(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawRayTraceReflect(cmd, data, registry, state)) return;

	if (!DrawTemporalAccumulate(cmd, data, state)) return;

	if (data.temporalAccumulateColorTexture && data.temporalAccumulateHistoryColorTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateColorTexture, data.temporalAccumulateHistoryColorTexture);
	if (data.temporalAccumulateMomentTexture && data.temporalAccumulateHistoryMomentTexture)
		Texture2D::CopyTextureAsync(cmd, data.temporalAccumulateMomentTexture, data.temporalAccumulateHistoryMomentTexture);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	if (!DrawSpatialDenoising(cmd, data, state)) return;

	//if (!DrawScale(data, state)) return;
}

void RayTraceReflectPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceReflectOn);

	if (!ShouldExecute(registry, state))
		return;

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");

	{
		//光追参数
		RayTraceParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.tMin = std::max(0.f, state.option.rayTraceReflectParams.tMin),
			.tMax = std::max(0.f, state.option.rayTraceReflectParams.tMax),
			.maxBounce = std::max((uint32_t)1, std::min(state.option.rayTraceReflectParams.maxBounceLimit, GlobalConfig::RayTrace_Max_Bounce_limit)),
			.sampleRayCount = std::max((uint32_t)1, state.option.rayTraceReflectParams.NumSamples),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_RayTraceParamsUBO->WriteData(&params, sizeof(RayTraceParams));
		binding.SetUniformBlock(_RayTraceParamsUBO, 5);
	}
}

void RayTraceReflectPass::SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RayTraceReflectPass::DrawRayTraceReflect(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state)
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
	target->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");

	CubeParams cubeParams;
	std::shared_ptr<TextureCube> cubeMap;
	std::shared_ptr<TextureCube> IBLDiffuse;
	std::shared_ptr<TextureCube> IBLPrefilter;
	std::shared_ptr<Texture2D> IBLBrdfLUT;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
	{
		if (state.skyAtmosphereParams.skyCube && !state.skyAtmosphereParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyAtmosphereParams.skyCube;
		}

		if (
			state.skyAtmosphereParams.skyCubeDiffuse
			&& !state.skyAtmosphereParams.skyCubeDiffuse->IsEmpty()
			&& state.skyAtmosphereParams.skyCubePrefilter
			&& !state.skyAtmosphereParams.skyCubePrefilter->IsEmpty()
			&& state.skyAtmosphereParams.brdfLUT
			&& !state.skyAtmosphereParams.brdfLUT->IsEmpty()
			)
		{
			cubeParams.IBLEnable = true;
			IBLDiffuse = state.skyAtmosphereParams.skyCubeDiffuse;
			IBLPrefilter = state.skyAtmosphereParams.skyCubePrefilter;
			IBLBrdfLUT = state.skyAtmosphereParams.brdfLUT;
		}
	}
	else if (state.option.flags.skyboxOn)
	{
		if (state.skyboxParams.skyCube && !state.skyboxParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyboxParams.skyCube;
		}

		if (
			state.skyboxParams.skyCubeDiffuse
			&& !state.skyboxParams.skyCubeDiffuse->IsEmpty()
			&& state.skyboxParams.skyCubePrefilter
			&& !state.skyboxParams.skyCubePrefilter->IsEmpty()
			&& state.skyboxParams.brdfLUT
			&& !state.skyboxParams.brdfLUT->IsEmpty()
			)
		{
			cubeParams.IBLEnable = true;
			IBLDiffuse = state.skyboxParams.skyCubeDiffuse;
			IBLPrefilter = state.skyboxParams.skyCubePrefilter;
			IBLBrdfLUT = state.skyboxParams.brdfLUT;
		}
	}

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(cubeParams));
	paramsUBO->WriteDataAsync(cmd, &cubeParams, sizeof(cubeParams));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);

	if (!BindGeneralData(binding))
		return false;

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);


	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	binding.SetUniformBlock(paramsUBO, 6);
	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 11);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 12);
	binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 13);
	binding.SetUniformTexture(data.ssaoMap, vk::ImageAspectFlagBits::eColor, 14);

	if (cubeParams.CubeEnable)
		binding.SetUniformTextureCube(cubeMap, vk::ImageAspectFlagBits::eColor, 15);

	if (cubeParams.IBLEnable)
	{
		binding.SetUniformTextureCube(IBLDiffuse, vk::ImageAspectFlagBits::eColor, 16);
		binding.SetUniformTextureCube(IBLPrefilter, vk::ImageAspectFlagBits::eColor, 17);
		binding.SetUniformTexture(IBLBrdfLUT, vk::ImageAspectFlagBits::eColor, 18);
	}

	_rayTraceShader.Bind(cmd, binding);
	cmd->dispatch((data.drawSize.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (data.drawSize.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool RayTraceReflectPass::DrawTemporalAccumulate(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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
		.maxAccumulateCount = state.option.rayTraceReflectParams.maxAccumulateCount
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

bool RayTraceReflectPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	auto& source = data.temporalAccumulateColorTexture;
	auto& target = data.outPutTexture;
	auto& moment = data.temporalAccumulateMomentTexture;

	if (!source || source->IsEmpty())
		return false;

	if (!target || target->IsEmpty())
		return false;

	AtrousBilateralFilter::Params params{
		.normalFactor = state.option.rayTraceReflectParams.normalFactor,
		.depthFactor = state.option.rayTraceReflectParams.depthFactor,
		.luminanceFactor = state.option.rayTraceReflectParams.luminanceFactor
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
		state.option.rayTraceReflectParams.filterCount
	);

	cmd->SubmitToQueue();

	return true;
}


bool RayTraceReflectPass::DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	return true;
}

void RayTraceReflectPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool RayTraceReflectPass::BindGeneralData(ComputeBindingRecord& binding)
{
	if (!_buffers)
		return false;

	auto bindSSBO = [&binding](const BindingPoint& bp, const std::shared_ptr<StorageBlock>& ssbo) -> bool {
		if (!ssbo)
			return false;
		binding.SetStorageBlock(ssbo, bp);
		};

	return bindSSBO(BindingPoint{ .binding = 0,.set = 0 }, _buffers->GetTraiangles())
		&& bindSSBO(BindingPoint{ .binding = 1,.set = 0 }, _buffers->GetTraiangleExt())
		&& bindSSBO(BindingPoint{ .binding = 2,.set = 0 }, _buffers->GetMeshmatData())
		&& bindSSBO(BindingPoint{ .binding = 3,.set = 0 }, _buffers->GetWorldBVHNode())
		&& bindSSBO(BindingPoint{ .binding = 4,.set = 0 }, _buffers->GetMeshBVHNode());
}
