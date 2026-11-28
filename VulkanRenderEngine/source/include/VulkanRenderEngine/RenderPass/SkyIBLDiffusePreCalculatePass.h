#pragma once

#include <cstdint>
#include <memory>

#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

// 原始天空 IBL diffuse：对「不含云的背景 cube」（大气 skyCube 优先，其次天空盒）做辐照度卷积，
// 结果就是云环境项的物理来源 E_sky（半球积分后的天空辐照度，各方向同值，采样无方向 / 无粗糙度）。
//
// 与 IBLPreCalculatePass 的分工：
//   - 本 Pass 只对背景算 diffuse，排在 atmosphere / skybox 之后、cloud 之前；
//   - IBLPreCalculatePass 在云之后做合成（finalSkyCube），其 diffuse / prefilter 面向场景，
//     含云时不能反向喂给云（云照亮云 + 循环依赖），所以云的环境光只取这里的产物。
//
// 无背景（既无大气也无天空盒）时保持黑：云没有环境光，只剩直射项。
//
// 更新条件：背景 cube 指针或 revision 变化才重算（大气参数 / 太阳变化推 skyCubeRevision，
// 天空盒换图推自己的 revision），其余帧零成本。
class SkyIBLDiffusePreCalculatePass : public RenderPassBase
{
public:
	SkyIBLDiffusePreCalculatePass();
	virtual ~SkyIBLDiffusePreCalculatePass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override { return true; }
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	IBLImageBuilder _iblBuilder;

	std::shared_ptr<TextureCube> _skyDiffuse;	// 32³，rgba16f，单级 mip（构造时清零，无背景时即黑）

	std::shared_ptr<TextureCube> _lastSource;
	uint64_t _lastSourceRevision = 0;
	uint64_t _skyDiffuseRevision = 0;
	bool _generated = false;
};
