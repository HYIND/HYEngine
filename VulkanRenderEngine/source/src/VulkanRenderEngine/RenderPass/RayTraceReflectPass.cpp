#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RayTraceReflectPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

struct RayTraceParams
{
	glm::ivec2 screenSize;
	float tMin = 0.f;
	float tMax = 0.f;
	uint32_t maxBounce = 0;
	uint32_t sampleRayCount = 0;
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


RayTraceReflectPass::RayTraceReflectPass(
	const std::string& rayTraceComputerShaderPath,
	const std::string& spatialDenoisingComputerShaderPath,
	const std::string& temporalDenoisingComputerShaderPath,
	const std::string& scaleComputerShaderPath
)
	:
	_firstDrawTemporal(true),
	_enable(false)
{

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
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
			_rayTraceShader_useGbuffer.Create(config);
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

	RayTraceParamsUBO = std::make_shared<UniformBlock>(sizeof(RayTraceParams));
	SpatialDenoisingParamsUBO = std::make_shared<UniformBlock>(sizeof(SpatialDenoisingParams));
	TemporalAccumulateParamsUBO = std::make_shared<UniformBlock>(sizeof(TemporalAccumulateParams));

	_rayTraceShader_useGbuffer.SetUniformBlock(RayTraceParamsUBO, 5);
	_spatialDenoisingShader.SetUniformBlock(SpatialDenoisingParamsUBO, 0);
	_temporalDenoisingShader.SetUniformBlock(TemporalAccumulateParamsUBO, 0);
}

RayTraceReflectPass::~RayTraceReflectPass()
{
}

bool RayTraceReflectPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.rayTraceReflectParams.maxBounceLimit < 0)
		return false;
	return true;
}

void RayTraceReflectPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
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

	if (!DrawRayTraceGI(data, state)) return;
	if (!DrawSpatialDenoising(data, state)) return;
	if (!DrawTemporalDenoising(data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
		Texture2D::CopyTexture(data.outPutTexture, data.historyColorTexture);

	//if (!DrawScale(data, state)) return;

	//RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
	//	THREADCONTEXT->UnBind();
	//	auto task1 = CoroTask::Run([&]()-> void {DrawTexture(data.outPutTexture, "temp/test_RayTraceReflectPass.png"); });
	//	task1.sync_wait();
	//	THREADCONTEXT->Bind();
	//	});
}

void RayTraceReflectPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceReflectOn);
}

void RayTraceReflectPass::SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RayTraceReflectPass::DrawRayTraceGI(FrameRenderData& data, RenderState& state)
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

	auto& rayTraceShader = _rayTraceShader_useGbuffer;
	if (!BindGeneralData(rayTraceShader))
		return false;

	rayTraceShader.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta
	);

	//光追参数
	RayTraceParams params{
		.screenSize = data.drawSize,
		.tMin = std::max(0.f, state.option.rayTraceReflectParams.tMin),
		.tMax = std::max(0.f, state.option.rayTraceReflectParams.tMax),
		.maxBounce = std::max((uint32_t)1, std::min(state.option.rayTraceReflectParams.maxBounceLimit, GlobalConfig::RayTrace_Max_Bounce_limit)),
		.sampleRayCount = std::max((uint32_t)1, state.option.rayTraceReflectParams.NumSamples),
		.frameIndex = state.renderRecord.frameIndex % 100000
	};
	RayTraceParamsUBO->WriteData(&params, sizeof(RayTraceParams));


	rayTraceShader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	rayTraceShader.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	rayTraceShader.SetStorageImage(target, 6);
	rayTraceShader.SetUniformTexture(data.gPosition, 7);
	rayTraceShader.SetUniformTexture(data.gNormal, 8);
	rayTraceShader.SetUniformTexture(data.gAlbedoOpacity, 9);
	rayTraceShader.SetUniformTexture(data.gMetallicRoughness, 10);
	rayTraceShader.SetUniformTexture(data.sceneDepthBuffer, 11);
	rayTraceShader.SetUniformTexture(data.atlasShadowMap, 12);
	rayTraceShader.SetUniformTexture(data.ssaoMap, 13);

	rayTraceShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

bool RayTraceReflectPass::DrawSpatialDenoising(FrameRenderData& data, RenderState& state)
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


	SpatialDenoisingParams params{
	.screenSize = data.drawSize,
	.kernelSize = state.option.rayTraceReflectParams.BlurKernelSize,
	.sigma = state.option.rayTraceReflectParams.BlurGaussSigma,
	.blurRadius = state.option.rayTraceReflectParams.BlurRadius,
	.blurDepthWeight = state.option.rayTraceReflectParams.BlurDepthWeight
	};
	SpatialDenoisingParamsUBO->WriteData(&params, sizeof(params));

	_spatialDenoisingShader.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	_spatialDenoisingShader.SetStorageImage(target, 1);
	_spatialDenoisingShader.SetUniformTexture(data.gNormal, 2);
	_spatialDenoisingShader.SetUniformTexture(data.sceneDepthBuffer, 3);
	_spatialDenoisingShader.SetUniformTexture(source, 4);

	_spatialDenoisingShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

bool RayTraceReflectPass::DrawTemporalDenoising(FrameRenderData& data, RenderState& state)
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

	TemporalAccumulateParams params{
		.screenSize = data.drawSize,
		.initBlendFactor = state.option.rayTraceReflectParams.initBlendFactor,
		.dynamicBlendFactor = state.option.rayTraceReflectParams.dynamicBlendFactor
	};
	TemporalAccumulateParamsUBO->WriteData(&params, sizeof(params));

	_temporalDenoisingShader.SetStorageImage(target, 1);
	_temporalDenoisingShader.SetUniformTexture(source, 2);
	_temporalDenoisingShader.SetUniformTexture(data.historyColorTexture, 3);
	_temporalDenoisingShader.SetUniformTexture(data.gMotionVector, 4);

	_temporalDenoisingShader.Bind(cmd);
	cmd->dispatch((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

bool RayTraceReflectPass::DrawScale(FrameRenderData& data, RenderState& state)
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

void RayTraceReflectPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool RayTraceReflectPass::BindGeneralData(ComputePipeline& shader)
{
	if (!_buffers)
		return false;

	auto bindSSBO = [&shader](const BindingPoint& bp, const std::shared_ptr<StorageBlock>& ssbo) -> bool {
		if (!ssbo)
			return false;
		shader.SetStorageBlock(ssbo, bp);
		};

	return bindSSBO(BindingPoint{ .binding = 0,.set = 0 }, _buffers->GetTraiangles())
		&& bindSSBO(BindingPoint{ .binding = 1,.set = 0 }, _buffers->GetTraiangleExt())
		&& bindSSBO(BindingPoint{ .binding = 2,.set = 0 }, _buffers->GetMeshmatData())
		&& bindSSBO(BindingPoint{ .binding = 3,.set = 0 }, _buffers->GetWorldBVHNode())
		&& bindSSBO(BindingPoint{ .binding = 4,.set = 0 }, _buffers->GetMeshBVHNode());
}
