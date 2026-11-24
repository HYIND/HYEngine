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
