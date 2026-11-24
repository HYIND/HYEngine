#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGIPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

struct CubeParams {
	uint32_t CubeEnable = 0;
};

struct RayTraceParams
{
	float tMin = 0.f;
	float tMax = 0.f;
	uint32_t maxBounce = 0;
	uint32_t sampleRayCount = 0;
	float GIIntensity = 0.f;
	uint32_t frameIndex;
};


RTCoreRayTraceGIPass::RTCoreRayTraceGIPass(
	const std::string& raygenPath,
	const std::string& missPath,
	const std::string& closestHitPath,
	const std::string& anyHitPath,
	const std::string& intersectionPath,
	const std::string& callablePath
)
	:
	_enable(false)
{

	{
		RayTracingPipelineConfig config;
		config.raygenPath = raygenPath;
		config.missPath = missPath;
		config.extraMissPaths = { "shader/RTCoreRayTrace/ShadowMiss.rmiss" };	// miss 1：阴影光线（RayTraceGeneral.comp 的 TraceShadowRay）
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
			.AddStorageImage(16);		// 镜面那一路的追踪结果（alpha = 第一次反弹的命中距离）

		if (config.Validate())
			_rayTraceShader.Create(config);
	}

	_RayTraceParamsUBO = std::make_shared<UniformBlock>(sizeof(RayTraceParams));
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

	// Temp 0 ~ 4：漫反射（追踪 / 时域颜色 / 时域矩 / 空域乒乓 / 空域结果），5 ~ 9：镜面，顺序相同
	// Persistent 0 ~ 1：漫反射历史（颜色 / 矩），2 ~ 3：镜面历史
	auto bindSignal = [&](IndirectSignalDenoiser::Textures& tex, uint32_t tempBase, uint32_t persistentBase) {
		tex.origin = ctx.GetTemp(tempBase + 0);
		tex.temporalColor = ctx.GetTemp(tempBase + 1);
		tex.temporalMoment = ctx.GetTemp(tempBase + 2);
		tex.spatialTemp = ctx.GetTemp(tempBase + 3);
		tex.output = ctx.GetTemp(tempBase + 4);
		tex.historyColor = ctx.GetPersitent(persistentBase + 0);
		tex.historyMoment = ctx.GetPersitent(persistentBase + 1);
	};
	bindSignal(data.diffuse, 0, 0);
	bindSignal(data.specular, 5, 2);

	data.outPutTexture = ctx.GetOutput(0);

	auto cmd = cmdCtx.GetCmd();

	if (!DrawRayTraceGI(cmd, data, registry, state)) return;

	if (!DrawDenoise(cmd, data, state)) return;

	if (!DrawUpsample(cmd, data, state)) return;

	//if (!DrawScale(data, state)) return;
}

void RTCoreRayTraceGIPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceGIOn);

	if (!ShouldExecute(registry, state))
		return;

	auto& binding = registry.Get<RayTracingBindingRecord>("binding");

	{
		//光追参数
		RayTraceParams params{
			.tMin = std::max(0.f, state.option.rayTraceGIParams.tMin),
			.tMax = std::max(0.f, state.option.rayTraceGIParams.tMax),
			.maxBounce = std::max((uint32_t)1, std::min(state.option.rayTraceGIParams.maxBounceLimit, GlobalConfig::RayTrace_Max_Bounce_limit)),
			.sampleRayCount = std::max((uint32_t)1, state.option.rayTraceGIParams.NumSamples),
			.GIIntensity = std::max(0.01f, state.option.rayTraceGIParams.GIIntensity),
			.frameIndex = state.renderRecord.frameIndex % 100000
		};
		_RayTraceParamsUBO->WriteData(&params, sizeof(RayTraceParams));
		binding.SetUniformBlock(_RayTraceParamsUBO, 5);
	}

}

void RTCoreRayTraceGIPass::SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RTCoreRayTraceGIPass::DrawRayTraceGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& target = data.diffuse.origin;
	auto& specularTarget = data.specular.origin;

	if (!target || target->IsEmpty() || !specularTarget || specularTarget->IsEmpty())
		return false;

	auto size = target->GetSize();
	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(target->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	specularTarget->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	cmd->clearColorImage(specularTarget->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	target->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	specularTarget->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	auto& binding = registry.Get<RayTracingBindingRecord>("binding");

	CubeParams cubeParams;
	std::shared_ptr<TextureCube> cubeMap;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
	{
		if (state.skyAtmosphereParams.skyCube && !state.skyAtmosphereParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyAtmosphereParams.skyCube;
		}
	}
	else if (state.option.flags.skyboxOn)
	{
		if (state.skyboxParams.skyCube && !state.skyboxParams.skyCube->IsEmpty())
		{
			cubeParams.CubeEnable = true;
			cubeMap = state.skyboxParams.skyCube;
		}
	}

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(cubeParams));
	paramsUBO->WriteDataAsync(cmd, &cubeParams, sizeof(cubeParams));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);

	if (!BindAccelerationStructure(binding))
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
	binding.SetStorageImage(specularTarget, vk::ImageAspectFlagBits::eColor, 16);
	binding.SetUniformTexture(data.gPosition, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(data.gNormal, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(data.gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetUniformTexture(data.gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 11);
	binding.SetUniformTexture(data.gDepthStencil, vk::ImageAspectFlagBits::eDepth, 12);
	binding.SetUniformTexture(data.atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 13);
	binding.SetUniformTexture(data.ssaoMap, vk::ImageAspectFlagBits::eColor, 14);

	if (cubeParams.CubeEnable)
		binding.SetUniformTextureCube(cubeMap, vk::ImageAspectFlagBits::eColor, 15);

	_rayTraceShader.Bind(cmd, binding);
	auto regionData = _rayTraceShader.GetSBTData();
	cmd->traceRaysKHR(regionData.raygenRegion, regionData.missRegion, regionData.hitRegion, regionData.callableRegion, size.x, size.y, 1);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceGIPass::DrawDenoise(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	const auto& giParams = state.option.rayTraceGIParams;

	IndirectSignalDenoiser::GBuffer gbuffer{
		.position = data.gPosition,
		.normal = data.gNormal,
		.depth = data.gDepthStencil,
		.motionVector = data.gMotionVector,
		.prevPosition = data.gPrevPosition,
		.prevNormal = data.gPrevNormal,
		.prevDepth = data.gPrevDepthStencil,
		.metallicRoughness = data.gMetallicRoughness
	};

	TemporalAccumulate::Params diffuseParams{
		.maxAccumulateCount = giParams.maxAccumulateCount,
		.depthThreshold = giParams.depthThreshold,
		.positiondistThreshold = giParams.positiondistThreshold,
		.normalThreshold = giParams.normalThreshold,
		.motionWeightScale = giParams.motionWeightScale,
		.varianceClipGamma = giParams.varianceClipGamma,
		// 降噪调试看漫反射那一路（GI 的主体）
		.debugMode = state.option.denoiseDebug.ModeFor(Scene3DRenderOption::DenoiseDebugTarget::RayTraceGI)
	};

	// 镜面那一路：alpha 是第一次反弹的命中距离，时域累积额外做虚像重投影
	TemporalAccumulate::Params specularParams = diffuseParams;
	specularParams.debugMode = 0;
	specularParams.signalType = static_cast<uint32_t>(TemporalAccumulate::SignalType::Specular);

	AtrousBilateralFilter::Params filterParams{
		.normalFactor = giParams.normalFactor,
		.depthFactor = giParams.depthFactor,
		.luminanceFactor = giParams.luminanceFactor
	};

	if (!_diffuseDenoiser.Execute(cmd, gbuffer, data.diffuse, diffuseParams, filterParams, giParams.filterCount, state.camera.curUBO, state.camera.prevUBO))
		return false;
	return _specularDenoiser.Execute(cmd, gbuffer, data.specular, specularParams, filterParams, giParams.filterCount, state.camera.curUBO, state.camera.prevUBO);
}

bool RTCoreRayTraceGIPass::DrawUpsample(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	// 低分辨率降噪结果（漫反射 + 镜面两路）→ 全分辨率输出：联合双边上采样（按平面距离 / 法线拒绝跨边缘的点），两路用同一套权重。
	// 光追端已分别除以漫反射率 / 镜面反射率（RayTraceGI.rgen），这里各自乘回再相加
	const auto& diffuse = data.diffuse.output;
	const auto& specular = data.specular.output;
	if (!diffuse || diffuse->IsEmpty() || !specular || specular->IsEmpty() || !data.outPutTexture || data.outPutTexture->IsEmpty())
		return false;

	// 降噪调试：上采样漫反射那一路的调试热力图代替降噪结果（不重新调制），最终合成时替换场景颜色显示
	auto debugTexture = _diffuseDenoiser.GetDebugTexture();
	const bool debugView = debugTexture != nullptr;

	_upsample.Execute(
		cmd,
		debugView ? debugTexture : diffuse,
		data.gPosition,
		data.gNormal,
		data.gDepthStencil,
		data.gAlbedoOpacity,
		data.gMetallicRoughness,
		data.outPutTexture,
		debugView ? IndirectUpsample::Remodulation::None : IndirectUpsample::Remodulation::DiffuseSpecular,
		state.camera.curUBO,
		state.camera.prevUBO,
		specular
	);

	cmd->SubmitToQueue();

	return true;
}

bool RTCoreRayTraceGIPass::DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state)
{
	return true;
}

void RTCoreRayTraceGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
	{
		_diffuseDenoiser.ResetHistory();
		_specularDenoiser.ResetHistory();
	}
}

bool RTCoreRayTraceGIPass::BindAccelerationStructure(RayTracingBindingRecord& binding)
{
	if (!_buffers)
		return false;

	binding.SetAccelerationStructure(_buffers->GetAccelerationStructure(), 0);
	binding.SetStorageBlock(_buffers->GetInstancesInfosBlock(), 3);

	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetVertexBlock(), 1);
	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetIndexBlock(), 2);

	// 蒙皮顶点：命中着色器按 instanceInfo.skinned 决定要不要从这里取顶点。
	// 没有蒙皮网格的帧绑静态顶点块兜底（skinned==0 时不会读）。
	auto skinnedVertexBlock = _buffers->GetSkinnedVertexBlock();
	binding.SetStorageBlock(skinnedVertexBlock ? skinnedVertexBlock : IndirectDrawManager::Instance()->GetVertexBlock(), 4);

	return true;
}

void RTCoreRayTraceGIPass::OnResize(uint32_t width, uint32_t height)
{
	// 渲染图按新尺寸重建，历史纹理是新分配的（内容未定义）：下一帧先清历史
	_diffuseDenoiser.ResetHistory();
	_specularDenoiser.ResetHistory();
}
