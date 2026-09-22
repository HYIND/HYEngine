#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGIPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

struct RayTraceParams
{
	glm::ivec2 screenSize;
	float tMin = 0.f;
	float tMax = 0.f;
	uint32_t maxBounce = 0;
	uint32_t sampleRayCount = 0;
	float GIIntensity = 0.f;
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


RTCoreRayTraceGIPass::RTCoreRayTraceGIPass(
	const std::string& raygenPath,
	const std::string& missPath,
	const std::string& closestHitPath,
	const std::string& anyHitPath,
	const std::string& intersectionPath,
	const std::string& callablePath,
	const std::string& spatialDenoisingComputerShaderPath,
	const std::string& temporalDenoisingComputerShaderPath,
	const std::string& scaleComputerShaderPath
)
	:
	_firstDrawTemporal(true),
	_enable(false)
{

	{
		RayTracingPipelineConfig config;
		//config.AddDefineMacro("Max_Recursive_Depth", GlobalConfig::RayTrace_Max_Recursive_Depth);
		//config.AddDefineMacro("Max_Bounce_limit", GlobalConfig::RayTrace_Max_Bounce_limit);
		config.raygenPath = raygenPath;
		config.missPath = missPath;
		config.closestHitPath = closestHitPath;
		config.anyHitPath = anyHitPath;
		config.intersectionPath = intersectionPath;
		config.callablePath = callablePath;

		config
			.AddCameraUnifromDataBinding()
			.AddLightDataBinding()
			.AddBindlessMaterialTextureBinding()
			.AddAccelerationStructure(0)
			.AddStorageBuffer(1)
			.AddStorageBuffer(2)
			.AddStorageBuffer(3)
			.AddUnifromBuffer(5)							// Param
			.AddStorageImage(6)								// outputImage
			.AddUnifromTexture(7)                           // gPosition
			.AddUnifromTexture(8)                           // gNormal
			.AddUnifromTexture(9)                           // gAlbedoOpacity
			.AddUnifromTexture(10)                          // gMetallicRoughness
			.AddUnifromTexture(11)                          // depthMap
			.AddUnifromTexture(12)                          // atlasShadowMap
			.AddUnifromTexture(13);                         // SSAOMap

		if (config.Validate())
			_rayTraceShader.Create(config);
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


	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = scaleComputerShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddStorageImage(2);

		if (config.Validate())
			_scaleShader.Create(config);
	}

	_RayTraceParamsUBO = std::make_shared<UniformBlock>(sizeof(RayTraceParams));
	_SpatialDenoisingParamsUBO = std::make_shared<UniformBlock>(sizeof(SpatialDenoisingParams));
	_TemporalAccumulateParamsUBO = std::make_shared<UniformBlock>(sizeof(TemporalAccumulateParams));

	_rayTraceShaderBinding.SetUniformBlock(_RayTraceParamsUBO, 5);
	_spatialDenoisingShaderBinding.SetUniformBlock(_SpatialDenoisingParamsUBO, 0);
	_temporalDenoisingShaderBinding.SetUniformBlock(_TemporalAccumulateParamsUBO, 0);
}

RTCoreRayTraceGIPass::~RTCoreRayTraceGIPass()
{}

bool RTCoreRayTraceGIPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.rayTraceGIParams.maxBounceLimit < 0)
		return false;
	return true;
}

void RTCoreRayTraceGIPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
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

	data.sceneDepthBuffer = ctx.GetExternal(0);

	data.originTexture = ctx.GetTemp(0);
	data.spatialDenoisingTexture = ctx.GetTemp(1);

	data.historyColorTexture = ctx.GetPersitent(0);

	data.outPutTexture = ctx.GetOutput(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawRayTraceGI(cmd, data, state)) return;
	if (!DrawSpatialDenoising(cmd, data, state)) return;
	if (!DrawTemporalDenoising(cmd, data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
	{
		Texture2D::CopyTextureAsync(cmd, data.outPutTexture, data.historyColorTexture);
		cmd->SubmitToQueue();
	}

	//if (!DrawScale(data, state)) return;
}

void RTCoreRayTraceGIPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceGIOn);

	if (!ShouldExecute(registry, state))
		return;

	{
		//光追参数
		RayTraceParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.tMin = std::max(0.f, state.option.rayTraceGIParams.tMin),
			.tMax = std::max(0.f, state.option.rayTraceGIParams.tMax),
			.maxBounce = std::max((uint32_t)1, std::min(state.option.rayTraceGIParams.maxBounceLimit, GlobalConfig::RayTrace_Max_Bounce_limit)),
			.sampleRayCount = std::max((uint32_t)1, state.option.rayTraceGIParams.NumSamples),
			.GIIntensity = std::max(0.01f, state.option.rayTraceGIParams.GIIntensity),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_RayTraceParamsUBO->WriteData(&params, sizeof(RayTraceParams));
	}

	{
		SpatialDenoisingParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.kernelSize = state.option.rayTraceGIParams.BlurKernelSize,
			.sigma = state.option.rayTraceGIParams.BlurGaussSigma,
			.blurRadius = state.option.rayTraceGIParams.BlurRadius,
			.blurDepthWeight = state.option.rayTraceGIParams.BlurDepthWeight
		};
		_SpatialDenoisingParamsUBO->WriteData(&params, sizeof(SpatialDenoisingParams));
	}

	{
		TemporalAccumulateParams params{
			.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height),
			.initBlendFactor = state.option.rayTraceGIParams.initBlendFactor,
			.dynamicBlendFactor = state.option.rayTraceGIParams.dynamicBlendFactor
		};
		_TemporalAccumulateParamsUBO->WriteData(&params, sizeof(TemporalAccumulateParams));
	}

}

void RTCoreRayTraceGIPass::SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RTCoreRayTraceGIPass::DrawRayTraceGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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
	target->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	auto& rayTraceShader = _rayTraceShader;
	auto& binding = _rayTraceShaderBinding;
	if (!BindAccelerationStructure(binding))
		return false;

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta
	);

	//binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	//binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 6);
	//binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 7);
	//binding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 8);
	//binding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 9);
	//binding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 10);
	//binding.SetUniformTexture(data.sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 11);
	//binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 12);
	//binding.SetUniformTexture(data.ssaoMap, vk::ImageAspectFlagBits::eColor, 13);

	rayTraceShader.Bind(cmd, binding);
	auto regionData = rayTraceShader.GetSBTData();
	//cmd->traceRaysKHR(regionData.raygenRegion, regionData.missRegion, regionData.hitRegion, regionData.callableRegion, data.drawSize.x, data.drawSize.y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceGIPass::DrawSpatialDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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
	_spatialDenoisingShaderBinding.SetUniformTexture(data.sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 3);
	_spatialDenoisingShaderBinding.SetUniformTexture(source, vk::ImageAspectFlagBits::eColor, 4);

	_spatialDenoisingShader.Bind(cmd, _spatialDenoisingShaderBinding);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceGIPass::DrawTemporalDenoising(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
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

bool RTCoreRayTraceGIPass::DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	//std::shared_ptr<Texture2D> srcTex;
	//std::shared_ptr<Texture2D>& targetTex = data.outPutTexture;

	//if (data.outPutTexture)
	//	srcTex = data.outPutTexture;
	//else
	//	srcTex = data.originTexture;

	//if (!srcTex || srcTex->IsEmpty())
	//	return false;

	//int srcWidth = srcTex->GetWidth();
	//int srcHeight = srcTex->GetHeight();

	//if (!targetTex || targetTex->IsEmpty())
	//	return false;

	//GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	//glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	//int destWidth = targetTex->GetWidth();
	//int destHeight = targetTex->GetHeight();

	//if (srcWidth == destWidth && srcHeight == destHeight)
	//{
	//	Texture2D::CopyTexture(srcTex, targetTex);
	//}
	//else
	//{
	//	_scaleShader.Use();

	//	_scaleShader.setIVec2("srcScreenSize", glm::ivec2(srcWidth, srcHeight));
	//	_scaleShader.setIVec2("destScreenSize", glm::ivec2(destWidth, destHeight));

	//	glBindImageTexture(0, srcTex->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	//	glBindImageTexture(1, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	//	glDispatchCompute((destWidth + work_size_x - 1) / work_size_x, (destHeight + work_size_y - 1) / work_size_y, 1);
	//	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	//}

	return true;
}

void RTCoreRayTraceGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool RTCoreRayTraceGIPass::BindAccelerationStructure(RayTracingBindingRecord& binding)
{
	if (!_buffers)
		return false;

	binding.SetAccelerationStructure(_buffers->GetAccelerationStructure(), 0);
	binding.SetStorageBlock(_buffers->GetInstancesInfosBlock(), 3);

	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetVertexBlock(), 1);
	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetIndexBlock(), 2);

	return true;
}
