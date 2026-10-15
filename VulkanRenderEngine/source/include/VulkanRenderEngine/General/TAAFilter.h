#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"

class TAAFilter
{
public:
	struct alignas(16) Params
	{
		uint32_t historyValid = 0;
		float historyWeight = 0.9f;
		float motionWeightScale = 0.05f;
		float depthThreshold = 0.02f;
		float normalThreshold = 0.85f;
	};

	TAAFilter(const std::string& shaderPath = "shader/Temporal/TAAFilter.comp");

	static glm::vec2 GetJitterPixels(uint32_t frameIndex);

	void Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& currentColor,
		const std::shared_ptr<Texture2D>& historyColor,
		const std::shared_ptr<Texture2D>& currentDepth,
		const std::shared_ptr<Texture2D>& currentNormal,
		const std::shared_ptr<Texture2D>& motionVector,
		const std::shared_ptr<Texture2D>& previousDepth,
		const std::shared_ptr<Texture2D>& previousNormal,
		const std::shared_ptr<Texture2D>& outputColor,
		const Params& params,
		std::shared_ptr<UniformBlock>& currentCameraUBO,
		std::shared_ptr<UniformBlock>& previousCameraUBO
	);

private:
	ComputePipeline _pipeline;
};
