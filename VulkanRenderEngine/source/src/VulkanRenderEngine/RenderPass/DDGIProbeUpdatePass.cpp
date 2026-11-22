#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIProbeUpdatePass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/GlobalConfig.h"

DDGIProbeUpdatePass::DDGIProbeUpdatePass(
	const std::string& raygenPath,
	const std::string& missPath,
	const std::string& closestHitPath,
	const std::string& anyHitPath,
	const std::string& intersectionPath,
	const std::string& callablePath
)
{
	RayTracingPipelineConfig config;
	config.raygenPath = raygenPath;
	config.missPath = missPath;
	config.closestHitPath = closestHitPath;
	config.anyHitPath = anyHitPath;
	config.intersectionPath = intersectionPath;
	config.callablePath = callablePath;
	DDGIAddLayoutDefines(config);

	config
		.AddLightDataBinding()						// set3：灯光
		.AddBindlessMaterialTextureBinding()		// set2：材质 + bindless 纹理
		.AddAccelerationStructure(0)				// TLAS
		.AddStorageBuffer(1)						// 顶点
		.AddStorageBuffer(2)						// 索引
		.AddStorageBuffer(3)						// 实例信息
		.AddStorageBuffer(4)						// 蒙皮顶点
		.AddUnifromBuffer(5)						// DDGIUniforms
		.AddStorageImage(7)							// rayData
		.AddUnifromTexture(8)						// 辐照度 history
		.AddUnifromTexture(9)						// 深度 history
		.AddUnifromTexture(15);						// 天空 cubemap

	if (config.Validate())
		_rayTraceShader.Create(config);

	_paramsUBO = std::make_shared<UniformBlock>(sizeof(DDGIUniforms));
}

DDGIProbeUpdatePass::~DDGIProbeUpdatePass()
{}

bool DDGIProbeUpdatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return _enable && _buffers != nullptr && _sharedState != nullptr;
}

void DDGIProbeUpdatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state) || !_framePrepared)
		return;

	auto rayData = ctx.GetOutput(0);
	auto irradianceHistory = ctx.GetPersitent(0);
	auto depthHistory = ctx.GetPersitent(1);

	if (!rayData || rayData->IsEmpty()
		|| !irradianceHistory || irradianceHistory->IsEmpty()
		|| !depthHistory || depthHistory->IsEmpty())
		return;

	auto cmd = cmdCtx.GetCmd();

	// 天空回退：来源与优先级同 RTCoreRayTraceGIPass
	std::shared_ptr<TextureCube> skyCube;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
		skyCube = state.skyAtmosphereParams.skyCube;
	else if (state.option.flags.skyboxOn)
		skyCube = state.skyboxParams.skyCube;
	const bool cubeEnable = skyCube && !skyCube->IsEmpty();

	const DDGIUniforms uniforms = DDGIBuildUniforms(_frame, state.option.ddgiParams, cubeEnable ? 1u : 0u, state.camera.position);
	_paramsUBO->WriteData(&uniforms, sizeof(uniforms));

	rayData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	irradianceHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	depthHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	auto& binding = registry.Get<RayTracingBindingRecord>("binding");
	if (!BindAccelerationStructure(binding))
		return;

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);
	binding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	binding.SetUniformBlock(_paramsUBO, 5);
	binding.SetStorageImage(rayData, vk::ImageAspectFlagBits::eColor, 7);
	// 全量重置帧 history 内容未定义，shader 由 runParams.z 保证不读
	binding.SetUniformTexture(irradianceHistory, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(depthHistory, vk::ImageAspectFlagBits::eColor, 9);
	if (cubeEnable)
		binding.SetUniformTextureCube(skyCube, vk::ImageAspectFlagBits::eColor, 15);

	_rayTraceShader.Bind(cmd, binding);
	auto regionData = _rayTraceShader.GetSBTData();
	cmd->traceRaysKHR(
		regionData.raygenRegion, regionData.missRegion, regionData.hitRegion, regionData.callableRegion,
		_launchSize.x, _launchSize.y, _launchSize.z
	);

	cmd->SubmitToQueue();

	_framePrepared = false;		// 一次性消费，防止同一帧重复派发
}

void DDGIProbeUpdatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	// 没有硬件光追时三个 DDGI Pass 一起关闭，否则混合 Pass 会把从未写过的 rayData 搬进 history
	_enable = state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable;
	_framePrepared = false;

	if (!ShouldExecute(registry, state))
		return;

	_frame = _sharedState->Resolve(
		state.renderRecord.frameIndex,
		state.option.ddgiParams,
		state.sceneBounds.min,
		state.sceneBounds.max,
		state.sceneBounds.valid != 0,
		state.camera.position
	);

	const DDGIVolume& volume = _frame.volume;
	if (volume.totalProbes == 0)
		return;

	if (_frame.shapeChanged)
	{
		const glm::vec3 extent = DDGIVolumeExtent(volume);
		const glm::vec3 center = volume.origin + extent * 0.5f;
		std::cout << std::format(
			"[ DDGI ] volume center=({:.2f}, {:.2f}, {:.2f}) extent=({:.2f}, {:.2f}, {:.2f}) "
			"spacing=({:.2f}, {:.2f}, {:.2f}) counts=({}, {}, {}) probes={}\n",
			center.x, center.y, center.z,
			extent.x, extent.y, extent.z,
			volume.spacing.x, volume.spacing.y, volume.spacing.z,
			volume.counts.x, volume.counts.y, volume.counts.z,
			volume.totalProbes);
	}

	// 平常帧只派发本帧相位上的探针；平移帧新进入窗口的探针不一定在相位上，派发全部、由 shader 过滤
	const uint32_t slice = std::max(1u, _frame.sliceCount);
	const uint32_t launchZ = _frame.scrolled ? volume.totalProbes : (volume.totalProbes + slice - 1) / slice;

	_launchSize = glm::uvec3(DDGIAtlasLayout::RayGridSize, DDGIAtlasLayout::RayGridSize, launchZ);
	_framePrepared = true;
}

void DDGIProbeUpdatePass::SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer)
{
	_buffers = std::move(buffer);
}

void DDGIProbeUpdatePass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}

bool DDGIProbeUpdatePass::BindAccelerationStructure(RayTracingBindingRecord& binding)
{
	if (!_buffers)
		return false;

	binding.SetAccelerationStructure(_buffers->GetAccelerationStructure(), 0);
	binding.SetStorageBlock(_buffers->GetInstancesInfosBlock(), 3);
	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetVertexBlock(), 1);
	binding.SetStorageBlock(IndirectDrawManager::Instance()->GetIndexBlock(), 2);

	// 没有蒙皮网格时用普通顶点缓冲占位（同 RTCoreRayTraceGIPass）
	auto skinnedVertexBlock = _buffers->GetSkinnedVertexBlock();
	binding.SetStorageBlock(skinnedVertexBlock ? skinnedVertexBlock : IndirectDrawManager::Instance()->GetVertexBlock(), 4);

	return true;
}
