#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"

class BloomPass
{
public:
	BloomPass(const std::string& computeShaderPath, uint32_t width, uint32_t height);
	void Draw(std::shared_ptr<Texture2D>& brightColorBuffer);
	std::shared_ptr<Texture2D> GetBloomBlurMap();
	void Resize(uint32_t newWidth, uint32_t newHeight);

private:
	void init();

private:
	uint32_t _width;
	uint32_t _height;

	ComputePipeline _bloomBlurShader;
	std::array<std::shared_ptr<Texture2D>, 2> _pingpongColorBuffers;
	std::shared_ptr<Texture2D> _outPutTarget;
};