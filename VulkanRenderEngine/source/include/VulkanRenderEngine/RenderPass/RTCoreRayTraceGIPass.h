#pragma once

#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"
#include "VulkanRenderEngine/General/IndirectUpsample.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"
#include "VulkanRenderEngine/General/IndirectSignalDenoiser.h"

class RTCoreRayTraceGIPass :public RenderPassBase
{
public:
	RTCoreRayTraceGIPass(
		const std::string& raygenPath,
		const std::string& missPath,
		const std::string& closestHitPath,
		const std::string& anyHitPath,
		const std::string& intersectionPath,
		const std::string& callablePath
	);
	~RTCoreRayTraceGIPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void OnResize(uint32_t width, uint32_t height) override;	// 历史纹理按新尺寸重新分配，下一帧清历史

	void SetGeneralBuffer(std::shared_ptr<RTCoreRayTraceGeneralBuffer> buffer);

private:
	struct FrameRenderData
	{
		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gDepthStencil;

		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> ssaoMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> gPrevPosition;
		std::shared_ptr<Texture2D> gPrevNormal;
		std::shared_ptr<Texture2D> gPrevDepthStencil;

		std::shared_ptr<Texture2D> outPutTexture;

		// 路径追踪拆成漫反射 / 镜面两路，各自的追踪结果与降噪资源（低分辨率）；
		// 降噪后联合双边上采样 + 分别重新调制，合成到 outPutTexture（全分辨率）
		IndirectSignalDenoiser::Textures diffuse;
		IndirectSignalDenoiser::Textures specular;
	};

	bool DrawRayTraceGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state);
	bool DrawDenoise(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawUpsample(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawScale(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

	bool BindAccelerationStructure(RayTracingBindingRecord& binding);

private:
	RayTracingPipeline _rayTraceShader;
	ComputePipeline _scaleShader;

	ComputeBindingRecord _scaleShaderBinding;

	mutable bool _enable;

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<UniformBlock> _RayTraceParamsUBO;

	// 漫反射 / 镜面两路各一套时域 + 空域降噪（镜面那一路做虚像重投影）。SetEnable 是 const，要能重置历史
	mutable IndirectSignalDenoiser _diffuseDenoiser;
	mutable IndirectSignalDenoiser _specularDenoiser;
	IndirectUpsample _upsample;
};
