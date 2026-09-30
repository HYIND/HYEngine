#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"

class BloomPass
{
public:
	BloomPass(
		const std::string& bloomDownSampleShaderPath,
		const std::string& bloomUpSampleShaderPath
	);
	void Draw(std::shared_ptr<Texture2D>& brightColorBuffer, std::vector<std::shared_ptr<Texture2D>>& bloomMipBuffers);

private:
	uint32_t _width;
	uint32_t _height;

	ComputePipeline _bloomDownSampleShader;
	ComputePipeline _bloomUpSampleShader;
};