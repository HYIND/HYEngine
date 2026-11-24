#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class TemporalAccumulate
{
public:
	// 信号类型（见 Temporal/TemporalAccumulate.comp）
	enum class SignalType : uint32_t
	{
		Default = 0,	// 表面运动矢量重投影（漫反射 GI 等）
		Specular = 1,	// 镜面：输入 alpha 是第一次反弹的命中距离，额外做虚像重投影，需要 metallicRoughnessMap
	};

	struct alignas(16) Params
	{
		uint32_t maxAccumulateCount;
		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;
		// 历史裁剪：本帧快速历史 3×3 邻域的 YCoCg 均值 ± γ·σ；<= 0 关闭（见 Temporal/TemporalHistoryClamp.comp）
		float varianceClipGamma = 2.0f;
		// 调试输出（Scene3DRenderOption::DenoiseDebugMode）：0 = 关闭。非 0 时额外把热力图写到 GetDebugTexture()，
		// 正常的累积结果 / 矩照常输出，历史不受影响
		uint32_t debugMode = 0;
		uint32_t signalType = static_cast<uint32_t>(SignalType::Default);
		// 以下只在 Specular 时使用
		float hitDistanceThreshold = 0.0f;		// 命中距离判定的相对容差（shader 里再随粗糙度放宽到 3 倍）；<= 0 关闭（默认，反射内容边缘会闪）
		float virtualRoughnessStart = 0.2f;		// 粗糙度 <= 它：全部用虚像重投影
		float virtualRoughnessEnd = 0.75f;		// 粗糙度 >= 它：全部用表面重投影
		float fastHistoryLength = 6.0f;			// 快速历史（裁剪盒子）最多累积几帧，也是 anti-lag 时历史最多缩到的长度
		uint32_t fastHistoryValid = 0;			// 内部填写（快速历史图有没有有效内容），调用方不用管
		float specularMinAccumulate = 0.0f;		// 镜面信号：最光滑的表面最多累积几帧（粗糙度 0.05 → 0.5 过渡到 maxAccumulateCount）；<= 0 不限制（默认）
		float specularParallaxScale = 1.0f;		// 镜面信号：按视差缩短历史，历史期间允许反射偏移几个波瓣半角；越大越不缩（拖影多），<= 0 关闭
		float antilagStrength = 1.0f;			// 超出裁剪盒子时缩短历史的力度（不进 UBO，给历史裁剪 Pass 的 push constant）
	};

public:
	TemporalAccumulate(
		const std::string& temporalAccumulateShaderPath = "shader/Temporal/TemporalAccumulate.comp",
		const std::string& historyClampShaderPath = "shader/Temporal/TemporalHistoryClamp.comp");

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
		std::shared_ptr<UniformBlock>& prevCameraUBO,
		const std::shared_ptr<Texture2D>& metallicRoughnessMap = nullptr	// Specular 时读粗糙度；其他信号可以不给
	);

	// 上一次 Execute 的调试热力图（与输出同尺寸，RGBA16F）；那一次 debugMode 为 0 时返回空
	std::shared_ptr<Texture2D> GetDebugTexture() const;

private:
	ComputePipeline _pipeline;
	ComputePipeline _clampPipeline;		// 历史裁剪（用本帧快速历史的邻域，必须在累积之后单独一次 dispatch）
	std::shared_ptr<Texture2D> _debugTexture;		// 按需创建：开调试时按输出尺寸建，关调试后保留（下次直接复用）
	std::shared_ptr<Texture2D> _dummyDebugTexture;	// 不调试时绑定的 1×1 占位（shader 不会写它）
	// 快速历史（裁剪盒子的中心）：两张乒乓，按输出尺寸按需重建；_fastValid = 上一次写的那张有有效内容
	std::shared_ptr<Texture2D> _fastHistory[2];
	uint32_t _fastWriteIndex = 0;
	bool _fastValid = false;
	bool _debugWritten = false;
};