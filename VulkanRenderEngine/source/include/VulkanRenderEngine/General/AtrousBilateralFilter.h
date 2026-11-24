#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class AtrousBilateralFilter
{
public:
	struct Params
	{
		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 4.0;
		uint32_t specularMode = 0;			// 1 = 镜面信号：按粗糙度 / 命中距离（输入 alpha）限制滤波半径，需要 metallicRoughnessMap
		float specularRadiusScale = 1.0f;	// 镜面信号的半径倍数
	};

public:
	// SVGF：先用 varianceShaderPath 估计初始方差（短历史用空间方差），再迭代 À-trous，方差随颜色逐轮传递。
	// 方差的两张 R32F 乒乓图由本对象持有，按输出尺寸按需重建（各 Pass 各有一个实例，Graph 内串行执行）。
	AtrousBilateralFilter(
		const std::string& atrousComputeShaderPath = "shader/Filter/Atrous-BilateralFilter.comp",
		const std::string& varianceShaderPath = "shader/Filter/SVGF-Variance.comp");

	void Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& originImage,
		const std::shared_ptr<Texture2D>& normalMap,
		const std::shared_ptr<Texture2D>& depthMap,
		const std::shared_ptr<Texture2D>& momentsMap,
		const std::shared_ptr<Texture2D>& outputImage,
		const std::shared_ptr<Texture2D>& tempImage,
		const Params& params,
		std::shared_ptr<UniformBlock>& curCameraUBO,
		std::shared_ptr<UniformBlock>& prevCameraUBO,
		uint32_t filterCount,
		const std::shared_ptr<Texture2D>& metallicRoughnessMap = nullptr	// specularMode = 1 时读粗糙度
	);

private:
	void EnsureVarianceTextures(uint32_t width, uint32_t height);

private:
	ComputePipeline _pipeline;
	ComputePipeline _variancePipeline;
	std::shared_ptr<Texture2D> _variance[2];
};