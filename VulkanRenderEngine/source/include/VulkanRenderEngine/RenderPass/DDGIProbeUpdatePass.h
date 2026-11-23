#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 探针光线追踪：只追踪本帧更新列表里的探针，第 i 个探针的光线写进 rayData 的第 i 个 tile，
// 卷积与混合交给 DDGIBlendPass。命中着色器用 shader/DDGI/DDGIClosestHit.rchit（负 t = 背面）。
// 多次反弹与探针偏移直接读持久图集（在混合之前执行，读到的是上一帧的结果）。
// 光追之前先用 DDGIReset.comp 清零本帧的重置列表（滚动寻址下新进入的格子、整体重置的体积、被禁用的探针）。
class DDGIProbeUpdatePass :public RenderPassBase
{
public:
	DDGIProbeUpdatePass(
		const std::string& raygenPath,
		const std::string& missPath,
		const std::string& closestHitPath,
		const std::string& anyHitPath,
		const std::string& intersectionPath,
		const std::string& callablePath,
		const std::string& resetPath = "shader/DDGI/DDGIReset.comp"
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
	ComputePipeline _resetShader;

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<DDGISharedState> _sharedState;
};
