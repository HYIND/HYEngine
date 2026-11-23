#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class TemporalAccumulate
{
public:
	struct alignas(16) Params
	{
		uint32_t maxAccumulateCount;
		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;
		// 历史裁剪：当前帧 3×3 邻域 YCoCg 均值 ± γ·σ 的 AABB；<= 0 关闭（见 Temporal/TemporalAccumulate.comp）
		float varianceClipGamma = 2.5f;
		// 调试输出（Scene3DRenderOption::DenoiseDebugMode）：0 = 关闭。非 0 时额外把热力图写到 GetDebugTexture()，
		// 正常的累积结果 / 矩照常输出，历史不受影响
		uint32_t debugMode = 0;
	};

public:
	TemporalAccumulate(const std::string& temporalAccumulateShaderPath = "shader/Temporal/TemporalAccumulate.comp");

	void Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& originImage,
		const std::shared_ptr<Texture2D>& positionMap,
		const std::shared_ptr<Texture2D>& normalMap,
		const std::shared_ptr<Texture2D>& depthMap,
		const std::shared_ptr<Texture2D>& motionVectorMap,
		const std::shared_ptr<Texture2D>& prevPositionMap,
		const std::shared_ptr<Texture2D>& prevNormalMap,
		const std::shared_ptr<Texture2D>& prevDepthMap,
		const std::shared_ptr<Texture2D>& outputColorImage,
		const std::shared_ptr<Texture2D>& outputMomentImage,
		const std::shared_ptr<Texture2D>& historyColorImage,
		const std::shared_ptr<Texture2D>& historyMomentImage,
		const Params& params,
		std::shared_ptr<UniformBlock>& curCameraUBO,
		std::shared_ptr<UniformBlock>& prevCameraUBO
	);

	// 上一次 Execute 的调试热力图（与输出同尺寸，RGBA16F）；那一次 debugMode 为 0 时返回空
	std::shared_ptr<Texture2D> GetDebugTexture() const;

private:
	ComputePipeline _pipeline;
	std::shared_ptr<Texture2D> _debugTexture;		// 按需创建：开调试时按输出尺寸建，关调试后保留（下次直接复用）
	std::shared_ptr<Texture2D> _dummyDebugTexture;	// 不调试时绑定的 1×1 占位（shader 不会写它）
	bool _debugWritten = false;
};