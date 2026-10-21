#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class AtrousGaussFilter
{
public:
	struct alignas(16) Params
	{
		float sigma = 1.0;
		uint32_t kernelRadius = 2;
		uint32_t kernelStep = 1;
	};

public:
	AtrousGaussFilter(const std::string& computeShaderPath = "shader/Filter/Atrous-GaussFilter.comp");

	void Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& originImage,
		const std::shared_ptr<Texture2D>& outputImage,
		Params params
	);

private:
	ComputePipeline _pipeline;
};