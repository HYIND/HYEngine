#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanSwapchain.h"
#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKContext.h"

static void ExecuteCallbacks(std::vector<std::function<void()>>& callbacks)
{
	for (auto& callback : callbacks)
	{
		if (callback)
			callback();
	}
}

using namespace VKCore;

VulkanSwapchain::~VulkanSwapchain()
{
}

void VulkanSwapchain::Release()
{
	if (m_swapchain) {
		ExecuteCallbacks(m_callbacks_destroySwapchain);

		if (auto device = m_device.lock())
		{
			for (auto& view : m_swapchainImageViews)
			{
				if (view) vkDestroyImageView(device->GetHandle(), view, nullptr);
			}
			vkDestroySwapchainKHR(device->GetHandle(), m_swapchain, nullptr);
		}
	}

	m_device.reset();
	m_surface.reset();
	m_swapchain = VK_NULL_HANDLE;
	m_swapchainCreateInfo = vk::SwapchainCreateInfoKHR();
	m_swapchainImages.clear();
	m_swapchainImageViews.clear();
	m_currentImageIndex = 0;
	m_callbacks_createSwapchain.clear();
	m_callbacks_destroySwapchain.clear();
}

vk::Result VulkanSwapchain::Create(
	std::shared_ptr<VulkanDevice> device,
	std::shared_ptr<VulkanSurface> surface,
	const VkExtent2D& windowSize,
	bool limitFrameRate,
	vk::SwapchainCreateFlagsKHR flags
)
{
	Release();

	if (!device || !surface)
		return vk::Result::eErrorInitializationFailed;

	auto availableSurfaceFormats = GetAvailableSurfaceFormats(*surface, *device);
	if (availableSurfaceFormats.empty())
		return vk::Result::eErrorUnknown;

	auto [surfaceCapabilitiesResult, surfaceCapabilities] = device->GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface->GetHandle());
	if (surfaceCapabilitiesResult != vk::Result::eSuccess)
	{
		std::cout << std::format("[ VulkanSwapchain ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", to_string(surfaceCapabilitiesResult));
		return surfaceCapabilitiesResult;
	}


	// 在 min 和 max 之间，尽量取 min+1（实现三缓冲）
	uint32_t minCount = surfaceCapabilities.minImageCount;
	uint32_t maxCount = surfaceCapabilities.maxImageCount;

	// 如果 maxCount == 0 表示无上限，否则取 min+1 和 maxCount 中较小的
	uint32_t targetCount = minCount + 1;
	if (maxCount > 0 && targetCount > maxCount) {
		targetCount = maxCount;
	}


	m_swapchainCreateInfo.setMinImageCount(targetCount)
		.setImageExtent(
			surfaceCapabilities.currentExtent.width == -1 ?
			vk::Extent2D{
			glm::clamp(windowSize.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
			glm::clamp(windowSize.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height) } :
			surfaceCapabilities.currentExtent
		)
		.setImageArrayLayers(1)
		.setPreTransform(surfaceCapabilities.currentTransform);

	if (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
		m_swapchainCreateInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eInherit);
	else
	{
		for (size_t i = 0; i < 4; i++)
			if (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR(1 << i)) {
				m_swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR(uint32_t(surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR(1 << i)));
				break;
			}
	}

	m_swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
		m_swapchainCreateInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
	if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)
		m_swapchainCreateInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferDst;
	else
		std::cout << std::format("[ VulkanSwapchain ] WARNING\n vk::ImageUsageFlagBits::eTransferDst isn't supported!\n");


	auto SetSurfaceFormat = [&](vk::SurfaceFormatKHR surfaceFormat) -> bool {
		bool formatIsAvailable = false;
		;
		if (surfaceFormat.format != vk::Format::eUndefined) {
			for (auto& i : availableSurfaceFormats)
			{
				if (i.format == surfaceFormat.format &&
					i.colorSpace == surfaceFormat.colorSpace) {
					m_swapchainCreateInfo.imageFormat = i.format;
					m_swapchainCreateInfo.imageColorSpace = i.colorSpace;
					return true;
				}
			}
		}
		else
			for (auto& i : availableSurfaceFormats)
			{
				if (i.colorSpace == surfaceFormat.colorSpace) {
					m_swapchainCreateInfo.imageFormat = i.format;
					m_swapchainCreateInfo.imageColorSpace = i.colorSpace;
					return true;
				}
			}
		};

	vk::SurfaceFormatKHR expectedFormat1{ vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };
	vk::SurfaceFormatKHR expectedFormat2{ vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };

	if (!SetSurfaceFormat(expectedFormat1) &&
		!SetSurfaceFormat(expectedFormat2)) {
		//如果找不到上述图像格式和色彩空间的组合，那只能有什么用什么，采用availableSurfaceFormats中的第一组
		m_swapchainCreateInfo.imageFormat = availableSurfaceFormats[0].format;
		m_swapchainCreateInfo.imageColorSpace = availableSurfaceFormats[0].colorSpace;
		std::cout << std::format("[ VulkanSwapchain ] WARNING\nFailed to select a four-component UNORM surface format!\n");
	}

	auto [surfacePresentModeCountResult, surfacePresentModes] = device->GetPhysicalDevice().getSurfacePresentModesKHR(surface->GetHandle());
	if (surfacePresentModeCountResult != vk::Result::eSuccess || surfacePresentModes.empty())
	{
		std::cout << std::format("[ VulkanSwapchain ] ERROR\nFailed to get the surface present modes!\nError code: {}\n", to_string(surfacePresentModeCountResult));
		return surfacePresentModeCountResult;
	}

	m_swapchainCreateInfo.setPresentMode(vk::PresentModeKHR::eFifo);
	if (!limitFrameRate)
	{
		for (auto mode : surfacePresentModes)
		{
			if (mode == vk::PresentModeKHR::eMailbox) {
				m_swapchainCreateInfo.setPresentMode(vk::PresentModeKHR::eMailbox);
				break;
			}
		}
	}

	m_swapchainCreateInfo
		.setFlags(flags)
		.setSurface(surface->GetHandle())
		.setImageSharingMode(vk::SharingMode::eExclusive)
		.setClipped(vk::True);

	m_device = device;
	m_surface = surface;

	if (vk::Result result = Create_Internal(); result != vk::Result::eSuccess)
		return result;

	ExecuteCallbacks(m_callbacks_createSwapchain);

	return vk::Result::eSuccess;
}

vk::Format VKCore::VulkanSwapchain::GetImageFormat() const { return vk::Format(m_swapchainCreateInfo.imageFormat); }

vk::ColorSpaceKHR VKCore::VulkanSwapchain::GetImageColorSpace() const
{
	return m_swapchainCreateInfo.imageColorSpace;
}

uint32_t VulkanSwapchain::GetCurrentImageIndex() const { return m_currentImageIndex; }

uint32_t VulkanSwapchain::GetSwapchainImageCount() const { return m_swapchainImages.size(); }

const std::vector<vk::ImageView>& VulkanSwapchain::SwapchainImageView() const { return m_swapchainImageViews; }

const std::vector<vk::Image>& VKCore::VulkanSwapchain::SwapchainImage() const { return m_swapchainImages; }

vk::Result VulkanSwapchain::SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable)
{
	static VKWrapper::VKFence s_null_fence(nullptr);
	return SwapImage(semaphore_imageIsAvailable, s_null_fence);
}

vk::Result VulkanSwapchain::SwapImage(const VKWrapper::VKFence& fence_imageIsAvailable)
{
	static VKWrapper::VKSemaphore s_null_seamphore(nullptr);
	return SwapImage(s_null_seamphore, fence_imageIsAvailable);
}

vk::Result VKCore::VulkanSwapchain::SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable, const VKWrapper::VKFence& fence_imageIsAvailable)
{
	auto device = m_device.lock();
	if (!device)
		return vk::Result::eErrorInitializationFailed;

	//销毁旧交换链（若存在）
	if (m_swapchainCreateInfo.oldSwapchain &&
		m_swapchainCreateInfo.oldSwapchain != m_swapchain) {
		vkDestroySwapchainKHR(device->GetHandle(), m_swapchainCreateInfo.oldSwapchain, nullptr);
		m_swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
	}
	//获取交换链图像索引
	while (VkResult rawResult = vkAcquireNextImageKHR(device->GetHandle(), m_swapchain, UINT64_MAX, semaphore_imageIsAvailable.GetHandle(), fence_imageIsAvailable.GetHandle(), &m_currentImageIndex))
	{
		auto result = vk::Result(rawResult);
		switch (result) {
		case vk::Result::eSuboptimalKHR:
		case vk::Result::eErrorOutOfDateKHR:
			if (vk::Result result = RecreateSwapchain(); result != vk::Result::eSuccess)
				return result;
			break; //注意重建交换链后仍需要获取图像，通过break递归，再次执行while的条件判定语句
		default:
			outStream << std::format("[ graphicsBase ] ERROR\nFailed to acquire the next image!\nError code: {}\n", to_string(result));
			return result;
		}
	}
	return vk::Result::eSuccess;
}

vk::Result VulkanSwapchain::PresentImage(vk::PresentInfoKHR& presentInfo)
{
	auto device = m_device.lock();
	if (!device)
		return vk::Result::eErrorInitializationFailed;

	auto result = device->GetPresentQueue().presentKHR(presentInfo);

	switch (result) {
	case vk::Result::eSuccess:
		return vk::Result::eSuccess;
	case vk::Result::eSuboptimalKHR:
	case vk::Result::eErrorOutOfDateKHR:
		return RecreateSwapchain();
	default:
		outStream << std::format("[ VulkanSwapchain ] ERROR\nFailed to queue the image for presentation!\nError code: {}\n", to_string(result));
		return result;
	}
}

//该函数用于在渲染循环中呈现图像的常见情形
vk::Result VulkanSwapchain::PresentImage(const VKWrapper::VKSemaphore& semaphore_renderingIsOver) {
	vk::PresentInfoKHR presentInfo;
	presentInfo
		.setSwapchainCount(1)
		.setSwapchains(m_swapchain)
		.setImageIndices(m_currentImageIndex);

	auto handle = semaphore_renderingIsOver.GetHandle();
	if (semaphore_renderingIsOver.GetHandle() != VK_NULL_HANDLE)
		presentInfo.setWaitSemaphores(handle);

	return PresentImage(presentInfo);
}

void VulkanSwapchain::AddCallback_CreateSwapchain(std::function<void()> func) {
	m_callbacks_createSwapchain.push_back(func);
}

void VulkanSwapchain::AddCallback_DestroySwapchain(std::function<void()> func) {
	m_callbacks_destroySwapchain.push_back(func);
}

vk::Result VulkanSwapchain::Create_Internal()
{
	auto device = m_device.lock();
	auto surface = m_surface.lock();

	if (!device || !surface)
		return vk::Result::eErrorInitializationFailed;

	//std::cout << "Swapchain Create Info:\n"
	//	<< "  sType: " << m_swapchainCreateInfo.sType << "\n"
	//	<< "  surface: " << (void*)m_swapchainCreateInfo.surface << "\n"
	//	<< "  minImageCount: " << m_swapchainCreateInfo.minImageCount << "\n"
	//	<< "  imageFormat: " << m_swapchainCreateInfo.imageFormat << "\n"
	//	<< "  imageColorSpace: " << m_swapchainCreateInfo.imageColorSpace << "\n"
	//	<< "  imageExtent: " << m_swapchainCreateInfo.imageExtent.width << "x" << m_swapchainCreateInfo.imageExtent.height << "\n"
	//	<< "  imageArrayLayers: " << m_swapchainCreateInfo.imageArrayLayers << "\n"
	//	<< "  imageUsage: " << std::hex << m_swapchainCreateInfo.imageUsage << std::dec << "\n"
	//	<< "  imageSharingMode: " << m_swapchainCreateInfo.imageSharingMode << "\n"
	//	<< "  preTransform: " << m_swapchainCreateInfo.preTransform << "\n"
	//	<< "  compositeAlpha: " << m_swapchainCreateInfo.compositeAlpha << "\n"
	//	<< "  presentMode: " << m_swapchainCreateInfo.presentMode << "\n"
	//	<< "  clipped: " << m_swapchainCreateInfo.clipped << "\n"
	//	<< "  oldSwapchain: " << (void*)m_swapchainCreateInfo.oldSwapchain << "\n";

	auto [createSwapchainResult, swapchain] = device->GetHandle().createSwapchainKHR(m_swapchainCreateInfo);
	if (createSwapchainResult != vk::Result::eSuccess)
	{
		std::cout << std::format("[ VulkanSwapchain ] ERROR\nFailed to create a swapchain!\nError code: {}\n", to_string(createSwapchainResult));
		return createSwapchainResult;
	}

	m_swapchain = swapchain;

	//获取交换链图像
	auto [swapchainImageResult, swapchainImages] = device->GetHandle().getSwapchainImagesKHR(m_swapchain);
	if (swapchainImageResult != vk::Result::eSuccess || swapchainImages.empty())
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to get swapchain images!\nError code: {}\n", to_string(swapchainImageResult));
		return createSwapchainResult;
	}

	m_swapchainImages = swapchainImages;

	//创建image view
	m_swapchainImageViews.resize(m_swapchainImages.size());
	vk::ImageViewCreateInfo imageViewCreateInfo;
	imageViewCreateInfo
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(m_swapchainCreateInfo.imageFormat)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLevelCount(1)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
		);

	for (size_t i = 0; i < m_swapchainImages.size(); i++)
	{
		imageViewCreateInfo.image = m_swapchainImages[i];
		auto [result, imageView] = device->GetHandle().createImageView(imageViewCreateInfo);
		if (result != vk::Result::eSuccess) {
			std::cout << std::format("[ graphicsBase ] ERROR\nFailed to create a swapchain image view!\nError code: {}\n", to_string(result));
			return result;
		}
		m_swapchainImageViews[i] = imageView;
	}
	return vk::Result::eSuccess;
}

vk::Result VulkanSwapchain::RecreateSwapchain()
{
	auto device = m_device.lock();
	auto surface = m_surface.lock();

	if (!device || !surface)
		return vk::Result::eErrorInitializationFailed;

	auto [surfaceCapabilitiesResult, surfaceCapabilities] = device->GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface->GetHandle());
	if (surfaceCapabilitiesResult != vk::Result::eSuccess)
	{
		std::cout << std::format("[ VulkanSwapchain ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", to_string(surfaceCapabilitiesResult));
		return surfaceCapabilitiesResult;
	}

	if (surfaceCapabilities.currentExtent.width == 0 ||
		surfaceCapabilities.currentExtent.height == 0)
		return vk::Result::eSuboptimalKHR;

	m_swapchainCreateInfo
		.setImageExtent(surfaceCapabilities.currentExtent)
		.setOldSwapchain(m_swapchain);

	auto queue_graphics = device->GetGraphicsQueue();
	auto queue_presentation = device->GetPresentQueue();

	vk::Result result = queue_graphics.waitIdle();

	if (result == vk::Result::eSuccess && queue_graphics != queue_presentation)	//仅在等待图形队列成功，且图形与呈现所用队列不同时等待呈现队列
		result = queue_presentation.waitIdle();
	if (result != vk::Result::eSuccess) {
		std::cout << std::format("[ VulkanSwapchain ] ERROR\nFailed to wait for the queue to be idle!\nError code: {}\n", to_string(result));
		return result;
	}

	//销毁旧交换链相关对象
	ExecuteCallbacks(m_callbacks_destroySwapchain);
	for (auto& i : m_swapchainImageViews)
	{
		if (i) vkDestroyImageView(device->GetHandle(), i, nullptr);
	}
	m_swapchainImageViews.clear();

	//创建新交换链及与之相关的对象
	if (vk::Result result = Create_Internal(); result != vk::Result::eSuccess)
		return result;

	ExecuteCallbacks(m_callbacks_createSwapchain);

	return vk::Result::eSuccess;
}
