#pragma once

#include <cstdint>
#include <memory>

#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

// 独立 IBL 预计算 Pass：把多个天空源合成为一张最终天空 cube，再生成辐照度 cube、预过滤 cube 与 BRDF LUT。
// 输入（都是可选）：
//   - 云 cube：RGB = 云自身单散射 S，A = 透射率 T（CloudPreCalculatePass 产出，独立于背景）
//   - 背景 cube：大气优先，其次天空盒（L_bg）
// 合成（CloudSkyComposite.comp）：L_final = S + T * L_bg，写 finalSkyCube 的 level 0 后生成 mip，
// IBL 再从 finalSkyCube 采样。这样 mip 与合成后的最终颜色一致，避免分开生成 mip 的非线性误差。
// 只有背景、没有云时直接用背景 cube，不合成；两者都没有时 IBL 为空。
//
// finalSkyCube 同时通过 state.iblParams.finalSkyCube 暴露给反射 / GI 消费端，
// 保证反射天空与 IBL 用同一张 cube，且一致地显示云。
//
// 必须放在 Execute 里做：源 cube 由 FrameBegin 的生产 Pass 写入 state，
// RenderGraph 依赖保证本 Pass 在生产 Pass 之后、消费者之前执行。
class IBLPreCalculatePass : public RenderPassBase
{
public:
	IBLPreCalculatePass(const std::string& compositeShaderPath);
	virtual ~IBLPreCalculatePass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override { return true; }
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	enum class BackgroundKind : uint8_t { None = 0, Atmosphere = 1, SkyBox = 2 };

	// 本帧参与合成的输入快照。用指针 + revision 判断是否变化。
	struct SourceSelection {
		BackgroundKind background = BackgroundKind::None;
		std::shared_ptr<TextureCube> backgroundCube;
		uint64_t backgroundRevision = 0;
		std::shared_ptr<TextureCube> cloudCube;
		uint64_t cloudRevision = 0;

		bool operator==(const SourceSelection& o) const
		{
			return background == o.background
				&& backgroundCube == o.backgroundCube
				&& backgroundRevision == o.backgroundRevision
				&& cloudCube == o.cloudCube
				&& cloudRevision == o.cloudRevision;
		}
		bool operator!=(const SourceSelection& o) const { return !(*this == o); }
	};

	SourceSelection SelectSources(const RenderState& state) const;

private:
	IBLImageBuilder _iblBuilder;
	ComputePipeline _compositeShader;

	std::shared_ptr<TextureCube> _blackCube;		// 无背景时的 L_bg = 0 占位
	std::shared_ptr<TextureCube> _finalSkyCube;		// 云 + 背景合成结果（带 mip），IBL 与反射共用

	std::shared_ptr<TextureCube> _diffuse;
	std::shared_ptr<TextureCube> _prefilter;
	std::shared_ptr<Texture2D> _brdfLUT;

	SourceSelection _lastSelection;
};
