#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 探针光线追踪：每个线程一条光线，结果写进 rayData，卷积与混合交给 DDGIBlendPass。
// 加速结构等来自 RTCoreRayTraceGeneralPass；命中着色器用 shader/DDGI/DDGIClosestHit.rchit（负 t = 背面）。
// 多次反弹读上一帧的 history 图集（与 DDGIBlendPass 共享同一份持久资源）。
class DDGIProbeUpdatePass :public RenderPassBase
{
public:
	DDGIProbeUpdatePass(
		const std::string& raygenPath,
		const std::string& missPath,
		const std::string& closestHitPath,
		const std::string& anyHitPath,
		const std::string& intersectionPath,
		const std::string& callablePath
	);
	~DDGIProbeUpdatePass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer);
	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);

private:
	bool BindAccelerationStructure(RayTracingBindingRecord& binding);

private:
	RayTracingPipeline _rayTraceShader;

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<DDGISharedState> _sharedState;
	std::shared_ptr<UniformBlock> _paramsUBO;

	// FrameBegin 解析体积、定 launch 维度；UBO 与天空 cubemap 在 Execute 里处理：
	// 天空预计算是图内节点，FrameBegin 时 hasSkyAtmospherePreData 还是上一帧的值
	DDGIFrameState _frame{};
	glm::uvec3 _launchSize{ 0u };
	bool _framePrepared = false;

	bool _enable = false;
};
