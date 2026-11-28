#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/Texture3D.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

// 体积云天空 cube 参数。布局与 VolumeLut.comp / CloudCubeGenerate.comp 的 CloudParams 一致。
// 注意：CloudCubeGenerate.comp 的 RGB 是云自身散射 S，A 是透射率 T；背景合成不在这里做。
// 环境项不在参数里：云的环境光 = 天空辐照度（iblParams.skyDiffuse）× albedo，在 shader 内计算。
struct alignas(16) CloudCubeShaderParams
{
	glm::vec3 cloudAlbedo;
	float cloudBottomHeight;
	float cloudTopHeight;

	float coverage;
	float density;
	float shapeScale;
	float detailScale;
	float detailStrength;
	float phaseG;
	float multiScatterContribution;
	float multiScatterOcclusion;
	float multiScatterEccentricity;
	float powder;
	float maxTraceDistance;
	float erosionDepth;
	uint32_t viewStepCount;
	uint32_t lightStepCount;
	uint32_t multiScatterOctaves;
	float skyFillStrength;
	float skyFillDesaturation;
	float skyFillBottomOcclusion;
};
static_assert(sizeof(CloudCubeShaderParams) == 96, "CloudParams 布局要和 shader 一致");

// 体积云预计算 Pass：产出独立的云 cube（RGB=S，A=T），不读大气 / 天空盒背景，并上报 revision。
// 放在 Execute 里执行，IBLPreCalculatePass 依赖本节点，把云 cube 与当前背景 cube 合成最终天空后再生成 IBL。
// 这样云参数变化只重算云 cube，背景切换只重合成，不需要重跑云步进。
class CloudPreCalculatePass : public RenderPassBase
{
public:
	CloudPreCalculatePass(const std::string& cubeShaderPath);
	virtual ~CloudPreCalculatePass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override { return true; }
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	static CloudCubeShaderParams BuildCloudParams(const RenderState& state);
	static bool SameParams(const CloudCubeShaderParams& a, const CloudCubeShaderParams& b);

private:
	ComputePipeline _cubeGenerateShader;

	std::shared_ptr<TextureCube> _cloudCube;					// RGB=S，A=T；独立于大气 / 天空盒
	std::shared_ptr<Texture2D> _dummyTransmittanceLut;			// 只为满足绑定，AtmoVolumeEnable = false 时不会采样
	std::shared_ptr<Texture3D> _cloudShapeNoise;
	std::shared_ptr<Texture2D> _blueNoise;

	uint64_t _cloudCubeRevision = 0;

	bool _generated = false;
	CloudCubeShaderParams _lastParams{};
	std::shared_ptr<Texture2D> _lastTransmittanceLut;
	uint64_t _lastTransmittanceRevision = 0;
	uint64_t _lastSkyDiffuseRevision = 0;
};
