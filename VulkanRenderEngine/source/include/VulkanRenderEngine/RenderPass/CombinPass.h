#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"

constexpr uint32_t Max_Color_Buffer_Count = 10;

class CombinPass
{
public:
	CombinPass(const std::string& computerShaderPath);
	void Draw(std::shared_ptr<Texture2D>& destColorTexture, std::shared_ptr<Texture2D>& destBrightTexture, const std::vector<std::shared_ptr<Texture2D>>& colorBuffers);

private:
	ComputePipeline _shader;
};