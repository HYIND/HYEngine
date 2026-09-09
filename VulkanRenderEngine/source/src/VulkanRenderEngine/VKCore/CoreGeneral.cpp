#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\CoreGeneral.h"

using namespace VKCore;

std::vector<vk::SurfaceFormatKHR> VKCore::GetAvailableSurfaceFormats(VulkanSurface& surface, VulkanDevice& device)
{
	if (!surface.IsValid())
		return {};

	auto physicalDevice = device.GetPhysicalDevice();
	auto surfacehandle = surface.GetHandle();

	auto [result, surfaceFormats] = physicalDevice.getSurfaceFormatsKHR(surfacehandle);
	if (result != vk::Result::eSuccess || surfaceFormats.empty())
	{
		std::cout << std::format("[ GetAvailableSurfaceFormats ] ERROR\nFailed to get surface formats!\nError code: {}\n", to_string(result));
		return {};
	}

	return surfaceFormats;
}
