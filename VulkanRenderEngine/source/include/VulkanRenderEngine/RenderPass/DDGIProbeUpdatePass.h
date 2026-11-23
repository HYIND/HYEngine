#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 探针光线追踪：只追踪本帧更新列表里的探针，第 i 个探针的光线写进 rayData 的第 i 个 tile，
// 卷积与混合交给 DDGIBlendPass。命中着色器用 shader/DDGI/DDGIClosestHit.rchit（负 t = 背面）。
// 多次反弹与探针偏移读上一帧的 history（与 DDGIBlendPass 共享同一份持久资源，按上一帧布局寻址）。
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
};
