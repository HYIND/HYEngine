#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "RenderPassBase.h"

namespace Atmo
{
	struct alignas(16) AtmosphereParams
	{
		alignas(16) glm::vec3 DirLightColor;
		alignas(16) glm::vec3 DirLightDir;
		alignas(16) glm::vec3 CubeCapturePosition = glm::vec3(0, 500, 0);
		float PlanetRadius;
		float AtmosphereHeight;
		float RayleighScatteringScalarHeight;
		float MieScatteringScalarHeight;
		float MieAnisotropy;
		float OzoneLevelCenterHeight;
		float OzoneLevelWidth;
		uint32_t ScatterPathSampleCount;		// 沿着路径上的散射采样数
		uint32_t TransmittanceSampleCount;		// 对两点之间透射率计算的采样数
	};
}

class AtmospherePreCalculatePass : public RenderPassBase
{
public:
	AtmospherePreCalculatePass(
		const std::string& transmittanceLutShaderPath,
		const std::string& skyViewLutLutShaderPath,
		const std::string& skyCubeGenerateShaderPath
	);
	virtual ~AtmospherePreCalculatePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	void CaulateTransmittanceLut(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding);
	void CaulateSkyViewLut(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding, RenderState& state);
	void CaulateSkyCube(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, ComputeBindingRecord& binding);

private:
	IBLImageBuilder _iblBuilder;

	ComputePipeline _transmittanceLutShader;
	ComputePipeline _skyViewLutshader;
	ComputePipeline _skyCubeGenerateShader;

	std::shared_ptr<Texture2D> _transmittanceLut;
	std::shared_ptr<Texture2D> _skyViewLut;
	std::shared_ptr<TextureCube> _skyCube;
	std::shared_ptr<TextureCube> _skyCubeDiffuse;
	std::shared_ptr<TextureCube> _skyCubePrefilter;
	std::shared_ptr<Texture2D> _brdfLUT;

	std::shared_ptr<Atmo::AtmosphereParams> _lastParams;
};

class AtmospherePass :public RenderPassBase
{

public:
	AtmospherePass(const std::string& computeShaderPath);
	virtual ~AtmospherePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _shader;
};