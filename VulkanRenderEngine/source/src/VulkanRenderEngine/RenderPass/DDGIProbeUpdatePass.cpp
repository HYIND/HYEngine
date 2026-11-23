#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIProbeUpdatePass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/GlobalConfig.h"

namespace
{
	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kFrameKey = "ddgiUpdateFrame";
}

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
		.AddStorageBuffer(6)						// 体积表
		.AddStorageImage(7)							// rayData
		.AddUnifromTexture(8)						// 辐照度 history
		.AddUnifromTexture(9)						// 深度 history
		.AddUnifromTexture(10)						// probeData history
		.AddStorageBuffer(12)						// 更新列表
		.AddStorageBuffer(13)						// probeInfo
		.AddUnifromTexture(15);						// 天空 cubemap

	if (config.Validate())
		_rayTraceShader.Create(config);
}

DDGIProbeUpdatePass::~DDGIProbeUpdatePass()
{}

bool DDGIProbeUpdatePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.ddgiOn && GlobalConfig::RTCoreEnable && _buffers != nullptr && _sharedState != nullptr;
}

void DDGIProbeUpdatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto* framePtr = registry.TryLoad<DDGIFramePtr>(kFrameKey);
	if (!framePtr || !*framePtr)
		return;
	const DDGIFrame& frame = **framePtr;
	registry.Store(kFrameKey, DDGIFramePtr{});		// 一次性消费，防止同一帧重复派发
	if (!frame.enabled || frame.updateCount == 0)
		return;

	auto rayData = ctx.GetOutput(0);
	auto irradianceHistory = ctx.GetPersitent(0);
	auto depthHistory = ctx.GetPersitent(1);
	auto probeDataHistory = ctx.GetPersitent(2);

	if (!rayData || rayData->IsEmpty()
		|| !irradianceHistory || irradianceHistory->IsEmpty()
		|| !depthHistory || depthHistory->IsEmpty()
		|| !probeDataHistory || probeDataHistory->IsEmpty())
		return;

	auto cmd = cmdCtx.GetCmd();

	// 天空回退：来源与优先级同 RTCoreRayTraceGIPass。天空预计算是图内节点，必须在 Execute 里取
	std::shared_ptr<TextureCube> skyCube;
	if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
		skyCube = state.skyAtmosphereParams.skyCube;
	else if (state.option.flags.skyboxOn)
		skyCube = state.skyboxParams.skyCube;
	const bool cubeEnable = skyCube && !skyCube->IsEmpty();

	DDGIUniforms uniforms = frame.uniforms;
	uniforms.flags.y = cubeEnable ? 1u : 0u;
	// 主机可见、每帧一份：不等 GPU，也不会和在途帧抢同一块内存
	auto ubo = DDGIMakeHostBlock<UniformBlock>(&uniforms, sizeof(uniforms));

	rayData->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	irradianceHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	depthHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	probeDataHistory->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

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
	binding.SetUniformBlock(ubo, 5);
	binding.SetStorageBlock(frame.volumeTable, 6);
	binding.SetStorageImage(rayData, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(irradianceHistory, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(depthHistory, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(probeDataHistory, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetStorageBlock(frame.updateList, 12);
	binding.SetStorageBlock(frame.probeInfo, 13);
	if (cubeEnable)
		binding.SetUniformTextureCube(skyCube, vk::ImageAspectFlagBits::eColor, 15);

	_rayTraceShader.Bind(cmd, binding);
	auto regionData = _rayTraceShader.GetSBTData();
	cmd->traceRaysKHR(
		regionData.raygenRegion, regionData.missRegion, regionData.hitRegion, regionData.callableRegion,
		DDGIAtlasLayout::RayGridSize, DDGIAtlasLayout::RayGridSize, frame.updateCount
	);

	cmd->SubmitToQueue();
}

void DDGIProbeUpdatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	// 没有硬件光追时 DDGI 整体关闭，否则混合 Pass 会把从未写过的 rayData 搬进 history
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;
	registry.Store(kFrameKey, _sharedState->Resolve(state));
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
