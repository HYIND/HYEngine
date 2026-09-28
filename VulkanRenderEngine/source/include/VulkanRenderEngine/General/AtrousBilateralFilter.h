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
	};

public:
	AtrousBilateralFilter(const std::string& atrousComputeShaderPath);

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
		uint32_t filterCount
	);

private:
	ComputePipeline _pipeline;
};