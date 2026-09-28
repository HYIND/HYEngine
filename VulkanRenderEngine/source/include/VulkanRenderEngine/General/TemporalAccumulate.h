#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class TemporalAccumulate
{
public:
	struct Params
	{
		uint32_t maxAccumulateCount;
	};

public:
	TemporalAccumulate(const std::string& temporalAccumulateShaderPath);

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

private:
	ComputePipeline _pipeline;
};