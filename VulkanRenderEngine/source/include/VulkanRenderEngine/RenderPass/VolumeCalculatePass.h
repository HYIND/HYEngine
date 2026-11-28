#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "RenderPassBase.h"
#include "VulkanRenderEngine/General/TAAFilter.h"
#include "VulkanRenderEngine/General/AtrousGaussFilter.h"
#include <atomic>

// 体积效果（大气透视 / 指数高度雾 / 体积雾 / 体积云）拆成两个 Pass：
//   VolumeLutPass      只读 GBuffer、深度、运动矢量、阴影图集，算散射 / 透射率，再做 TAA + 高斯。
//                      不碰场景颜色，所以只依赖几何、SSAO、阴影、大气预计算，可以和光照 / GI / 降噪 / 间接光合成那一段并行。
//   VolumeCombinePass  不透明物体全部完成之后（opaqueFence），用上面的散射 / 透射率和场景颜色合成。
// 两者之间的散射图、透射率图、滤波后的散射图是 Lut 的 Output、Combine 的 Input。

// 这一帧体积要算哪些部分（Lut 和 Combine 用同一套判断，保证两边一致）
struct VolumePassFlags
{
	bool atmoEnable = false;
	bool exponentialHeightFogEnable = false;
	bool volumeFogEnable = false;
	bool cloudEnable = false;

	// 散射图是否需要 TAA + 高斯滤波（大气 / 体积雾 / 体积云任一开启时散射才有噪声）。
	// Lut 决定跑不跑滤波、Combine 决定读哪张散射图，都只看这一个值
	bool scatterFiltered = false;

	bool Any() const { return atmoEnable || exponentialHeightFogEnable || cloudEnable; }

	static VolumePassFlags FromState(RenderState& state);
};

// Lut 和 Combine 之间的本帧状态。两个 Pass 在渲染图里有 Lut → Combine 的依赖，不会同时访问
struct VolumePassShared
{
	std::atomic<bool> lutValid{ false };	// 本帧 Lut 是否完整算完（TAA / 拷历史 / 高斯任一步失败时为 false，Combine 跳过）
};

// ShouldExecute 两个 Pass 共用
bool VolumePassShouldExecute(RenderState& state);

// ---------------------------------------------------------------------------------------------------
// 指数高度雾参数：和 FogVolume.comp / VolumeCalculate.comp 的 FogParams 对应（std140）。
// 体积 Pass 和反射（光追反射 / SSR）都从同一个函数取，保证镜子里外的雾浓度、起始距离完全一致
// ---------------------------------------------------------------------------------------------------
struct alignas(16) ExponentialHeightFogShaderParams
{
	glm::vec3 fogAlbedo;
	float globalDensity;
	float fogStartHeight;
	float fogStartDistance;
	float fogHeightFalloff;
	uint32_t VolumeFogEnable;
	float FogMaxOpacity;
	uint32_t VolumeLightScatterPathSampleCount;
};
static_assert(sizeof(ExponentialHeightFogShaderParams) == 48);

// 主画面用的高度雾参数（体积雾开关的判断和 VolumeLutPass 一致：高度雾开 + 体积雾开 + 场景里有灯）
ExponentialHeightFogShaderParams BuildExponentialHeightFogShaderParams(RenderState& state);

// 反射光线上的解析高度雾（shader/Volume/ReflectionFog.comp 的 ReflectFogParams，std140）。
//   enable：主画面的指数高度雾开着（exponentialHeightFogOn）——和主画面同一个开关
//   volumeFogEnable = 0（或 reflectionVolumeMode == Off）：和主画面一样按 1 - exp(-光学厚度) 混到雾颜色
//   volumeFogEnable = 1：透射率同样解析；散射按 reflectionVolumeMode 计算方向光或全部光源（仅首跳最多 2 步分层抖动采样）
constexpr uint32_t kReflectionFogMaxSuns = 4;
constexpr uint32_t kReflectionFogMaxPointLights = 8;
constexpr uint32_t kReflectionFogMaxSpotLights = 8;
struct alignas(16) ReflectionFogShaderParams
{
	glm::vec3 fogAlbedo;
	float globalDensity;
	float fogStartHeight;
	float fogStartDistance;
	float fogHeightFalloff;
	uint32_t volumeFogEnable;
	float fogMaxOpacity;
	uint32_t enable;
	uint32_t sunCount;
	uint32_t pointCount;
	uint32_t spotCount;
	uint32_t padding0;
	uint32_t padding1;
	uint32_t padding2;
	glm::vec4 sunToLight[kReflectionFogMaxSuns];					// xyz：指向光源的单位方向；w：是否投射阴影（1 / 0）
	glm::vec4 sunRadiance[kReflectionFogMaxSuns];					// rgb：颜色 × 照度 × 体积散射强度
	glm::vec4 pointPosRadius[kReflectionFogMaxPointLights];			// xyz：位置；w：半径
	glm::vec4 pointRadianceCast[kReflectionFogMaxPointLights];		// rgb：颜色 × 强度 × 体积散射强度；w：是否投射阴影（1 / 0）
	glm::vec4 spotPosRadius[kReflectionFogMaxSpotLights];			// xyz：位置；w：半径
	glm::vec4 spotDirCos[kReflectionFogMaxSpotLights];				// xyz：照射方向（单位向量）；w：cutOff (cos)
	glm::vec4 spotRadianceOuter[kReflectionFogMaxSpotLights];		// rgb：颜色 × 强度 × 体积散射强度；w：outerCutOff (cos)
	glm::vec4 spotCastShadow[kReflectionFogMaxSpotLights];			// x：是否投射阴影（1 / 0）
};
static_assert(sizeof(ReflectionFogShaderParams) == 960);

ReflectionFogShaderParams BuildReflectionFogShaderParams(RenderState& state, Scene3DRenderOption::ReflectionFogVolumeMode volumeMode);

// 反射 / GI Pass 用：建一个本帧的 ReflectFogParams UBO（写入 + 屏障都在 cmd 上）
std::shared_ptr<UniformBlock> CreateReflectionFogUniformBlock(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, Scene3DRenderOption::ReflectionFogVolumeMode volumeMode);

class VolumeLutPass :public RenderPassBase
{
public:
	VolumeLutPass(const std::string& lutShaderPath, std::shared_ptr<VolumePassShared> shared);
	virtual ~VolumeLutPass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void OnResize(uint32_t width, uint32_t height) override;	// 历史纹理按新尺寸重新分配，下一帧清历史
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	struct FrameRenderData
	{
		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gDepthStencil;

		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> gPrevPosition;
		std::shared_ptr<Texture2D> gPrevNormal;
		std::shared_ptr<Texture2D> gPrevDepthStencil;

		// 输出（Combine 读）
		std::shared_ptr<Texture2D> scatterColor;
		std::shared_ptr<Texture2D> tramsmittance;
		std::shared_ptr<Texture2D> gaussScatterColor;

		// 本 Pass 内部
		std::shared_ptr<Texture2D> taaScatterColor;
		std::shared_ptr<Texture2D> taaHistoryScatterColor;

		VolumePassFlags flags;
	};

	void DrawLut(std::shared_ptr<RenderGraph::PassFrameCmd>& cmd, RenderGraph::FrameDataRegistry& registry, RenderState& state, FrameRenderData& data);
	bool DrawTAAFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);
	bool DrawGaussFilter(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, FrameRenderData& data);

	void SetTAAEnable(bool enable) const;

private:
	ComputePipeline _lutShader;

	// 云噪声只加载一次，整个生命周期复用
	std::shared_ptr<Texture3D> _cloudShapeNoise;
	std::shared_ptr<Texture2D> _blueNoise;

	TAAFilter _taaFilter;
	AtrousGaussFilter _gaussFilter;

	std::shared_ptr<VolumePassShared> _shared;

	mutable bool _firstDrawTemporal;
	mutable bool _taaEnable;
};

class VolumeCombinePass :public RenderPassBase
{
public:
	VolumeCombinePass(const std::string& combinShaderPath, std::shared_ptr<VolumePassShared> shared);
	virtual ~VolumeCombinePass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

private:
	ComputePipeline _combinShader;
	std::shared_ptr<VolumePassShared> _shared;
};
