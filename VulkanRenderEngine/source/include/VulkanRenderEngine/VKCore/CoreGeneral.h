#pragma once

#include "VulkanOutput.h"
#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h" 


namespace VKCore
{
	std::vector<vk::SurfaceFormatKHR> GetAvailableSurfaceFormats(VulkanSurface& surface, VulkanDevice& device);
}