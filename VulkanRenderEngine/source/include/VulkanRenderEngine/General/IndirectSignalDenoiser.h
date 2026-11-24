#pragma once

#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"

// 一路间接光信号的降噪：时域累积 → 写回历史 → À-trous 空域滤波。
// 路径追踪（RTGI / SSGI）拆成漫反射、镜面两路后每路一个实例（各自持有时域 / 空域的内部资源与「首帧清历史」状态）。
// 只用头文件实现：调用方的 .cpp 先包含 vkstdafx.h。
class IndirectSignalDenoiser
{
public:
	struct GBuffer
	{
		std::shared_ptr<Texture2D> position;
		std::shared_ptr<Texture2D> normal;
		std::shared_ptr<Texture2D> depth;
		std::shared_ptr<Texture2D> motionVector;
		std::shared_ptr<Texture2D> prevPosition;
		std::shared_ptr<Texture2D> prevNormal;
		std::shared_ptr<Texture2D> prevDepth;
		std::shared_ptr<Texture2D> metallicRoughness;	// 镜面信号的虚像重投影读粗糙度
	};

	struct Textures
	{
		std::shared_ptr<Texture2D> origin;				// 追踪结果（输入）
		std::shared_ptr<Texture2D> temporalColor;		// 时域累积结果（Temp）
		std::shared_ptr<Texture2D> temporalMoment;		// 时域矩（Temp）
		std::shared_ptr<Texture2D> spatialTemp;			// 空域滤波乒乓（Temp）
		std::shared_ptr<Texture2D> output;				// 空域滤波结果
		std::shared_ptr<Texture2D> historyColor;		// 历史颜色（Persistent）
		std::shared_ptr<Texture2D> historyMoment;		// 历史矩（Persistent）

		bool Valid() const
		{
			auto ok = [](const std::shared_ptr<Texture2D>& t) { return t && !t->IsEmpty(); };
			return ok(origin) && ok(temporalColor) && ok(temporalMoment) && ok(spatialTemp) && ok(output)
				&& ok(historyColor) && ok(historyMoment);
		}
	};

public:
	// 资源不全时返回 false，什么都不做
	bool Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const GBuffer& gbuffer,
		const Textures& textures,
		const TemporalAccumulate::Params& temporalParams,
		const AtrousBilateralFilter::Params& filterParams,
		uint32_t filterCount,
		std::shared_ptr<UniformBlock>& curCameraUBO,
		std::shared_ptr<UniformBlock>& prevCameraUBO)
	{
		if (!cmd || !textures.Valid())
			return false;

		if (_firstDraw)
		{
			vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			vk::ClearColorValue clearMoments = { 0.0f, 0.0f, 0.0f, 0.0f };
			vk::ImageSubresourceRange range;
			range.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
				.setBaseMipLevel(0)
				.setLevelCount(1);
			cmd->clearColorImage(textures.historyColor->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
			cmd->clearColorImage(textures.historyMoment->GetImage(), vk::ImageLayout::eGeneral, clearMoments, range);
			_firstDraw = false;
		}

		_temporal.Execute(
			cmd,
			textures.origin,
			gbuffer.position,
			gbuffer.normal,
			gbuffer.depth,
			gbuffer.motionVector,
			gbuffer.prevPosition,
			gbuffer.prevNormal,
			gbuffer.prevDepth,
			textures.temporalColor,
			textures.temporalMoment,
			textures.historyColor,
			textures.historyMoment,
			temporalParams,
			curCameraUBO,
			prevCameraUBO,
			gbuffer.metallicRoughness
		);
		cmd->SubmitToQueue();

		Texture2D::CopyTextureAsync(cmd, textures.temporalColor, textures.historyColor);
		Texture2D::CopyTextureAsync(cmd, textures.temporalMoment, textures.historyMoment);
		if (cmd->IsRecording())
			cmd->SubmitToQueue();

		_filter.Execute(
			cmd,
			textures.temporalColor,
			gbuffer.normal,
			gbuffer.depth,
			textures.temporalMoment,
			textures.output,
			textures.spatialTemp,
			filterParams,
			curCameraUBO,
			prevCameraUBO,
			filterCount
		);
		cmd->SubmitToQueue();

		return true;
	}

	// 下一次 Execute 先清空历史（Pass 重新开启时）
	void ResetHistory() { _firstDraw = true; }

	// 上一次时域累积的调试热力图（没开调试时为空）
	std::shared_ptr<Texture2D> GetDebugTexture() const { return _temporal.GetDebugTexture(); }

private:
	TemporalAccumulate _temporal;
	AtrousBilateralFilter _filter;
	bool _firstDraw = true;
};
