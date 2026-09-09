#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/General/RenderItem.h"

class GlobalPostProcessPass
{
public:
	GlobalPostProcessPass(const std::string& computeShaderPath);
	void Draw(
		std::shared_ptr<Texture2D> outPut,
		std::shared_ptr<Texture2D> colorBuffer,
		std::shared_ptr<Texture2D> bloomBlurMap,
		bool bloom_on, bool gamma_on, bool flipY,
		float exposureValue, float gammaValue
	);

private:
	ComputePipeline _shader;
};