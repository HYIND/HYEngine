#include "vkstdafx.h"
#include <DirectXTex/DirectXTex.h>
#include <dxgiformat.h>

using namespace DirectX;

struct DDSLoadResult {
	std::vector<uint8_t> data;
	uint32_t width = 0;
	uint32_t height = 0;
	vk::Format format;
	bool valid = false;
};


VkFormat DXGIToVulkanFormat(DXGI_FORMAT dxgiFormat);	// DXGI 到 Vulkan 格式转换函数
DXGI_FORMAT VulkanToDXGIFormat(VkFormat vkFormat);		// Vulkan 到 DXGI

DDSLoadResult LoadDDSFile(const std::string& filepath, vk::Format targetFormat);