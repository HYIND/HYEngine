#pragma once

#include <cstdint>
#include <memory>

#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

// 独立 IBL 预计算 Pass：天空源 Pass（大气 / 天空盒，后续还有云）只负责产出自己的 skyCube，
// 本 Pass 按优先级选一个源 cube，统一生成辐照度 cube、预过滤 cube 与 BRDF LUT，写入 state.iblParams。
//
// 必须放在 Execute 里做：RenderGraph 把所有 Pass 的 FrameBegin 并行提交，
// 在这里读生产 Pass 的 has*PreData / skyCubeRevision 必须等依赖节点的 FrameBegin 完成。
// 消费者（LightingPass / DDGIApply / 反射 Pass）同样在 Execute 里读 state.iblParams，
// 由渲染图依赖保证本 Pass 先于它们执行。
class IBLPreCalculatePass : public RenderPassBase
{
public:
	IBLPreCalculatePass();
	virtual ~IBLPreCalculatePass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override { return true; }
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	enum class SourceKind : uint8_t { None = 0, Atmosphere = 1, SkyBox = 2 };

	// 当前帧选中的源；大气优先，其次天空盒。没有 skyCube 时返回 None。
	SourceKind SelectSource(const RenderState& state, std::shared_ptr<TextureCube>& outCube, uint64_t& outRevision) const;

private:
	IBLImageBuilder _iblBuilder;

	std::shared_ptr<TextureCube> _diffuse;
	std::shared_ptr<TextureCube> _prefilter;
	std::shared_ptr<Texture2D> _brdfLUT;

	SourceKind _lastSource = SourceKind::None;
	std::shared_ptr<TextureCube> _lastCube;
	uint64_t _lastRevision = 0;
};
