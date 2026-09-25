#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "RenderPassBase.h"


class AtomspherePass :public RenderPassBase
{
private:
	struct alignas(16) AtomsphereParams
	{
		alignas(16) glm::vec3 DirLightColor;
		alignas(16) glm::vec3 DirLightDir;
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

public:
	AtomspherePass(const std::string& computeShaderPath, const std::string& transmittanceLutShaderPath, const std::string& skyViewLutLutShaderPath);
	virtual ~AtomspherePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	void CaulateTransmittanceLut(ComputeBindingRecord& binding);
	void CaulateSkyViewLut(ComputeBindingRecord& binding, RenderState& state);

private:
	ComputePipeline _shader;

	ComputePipeline _transmittanceLutShader;
	ComputePipeline _skyViewLutshader;

	std::shared_ptr<Texture2D> _transmittanceLut;
	std::shared_ptr<Texture2D> _skyViewLut;

	std::shared_ptr<AtomsphereParams> _lastParams;
};