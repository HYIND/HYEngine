#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"
#include "VulkanRenderEngine/General/IndirectSignalDenoiser.h"
#include "VulkanRenderEngine/General/IndirectUpsample.h"

class SSGIPass :public RenderPassBase
{
public:
	SSGIPass(const std::string& computerShaderPath);

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void OnResize(uint32_t width, uint32_t height) override;	// 历史纹理按新尺寸重新分配，下一帧清历史

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

		std::shared_ptr<Texture2D> sceneColorMap;
		std::shared_ptr<Texture2D> hzbDepthMap;
		std::shared_ptr<Texture2D> ssaoTexture;

		std::shared_ptr<Texture2D> outPutTexture;

		// 路径追踪拆成漫反射 / 镜面两路，各自的追踪结果与降噪资源（全分辨率）；
		// 降噪后重新调制并相加写到 outPutTexture（IndirectUpsample 同分辨率，DiffuseSpecular）
		IndirectSignalDenoiser::Textures diffuse;
		IndirectSignalDenoiser::Textures specular;
	};

	bool DrawSSGI(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderGraph::FrameDataRegistry& registry, RenderState& state);
	bool DrawDenoise(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);
	bool DrawCombine(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

private:
	ComputePipeline _ssgiShader;

	mutable bool _enable;

	std::shared_ptr<UniformBlock> _SSGIParamsUBO;

	// 漫反射 / 镜面两路各一套时域 + 空域降噪（镜面那一路做虚像重投影）。SetEnable 是 const，要能重置历史
	mutable IndirectSignalDenoiser _diffuseDenoiser;
	mutable IndirectSignalDenoiser _specularDenoiser;
	IndirectUpsample _combine;		// 同分辨率：两路分别乘回反射率再相加
};