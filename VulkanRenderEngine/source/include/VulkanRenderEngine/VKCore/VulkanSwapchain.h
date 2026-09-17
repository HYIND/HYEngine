#pragma once

#include "stdafx.h"
#include "VulkanSurface.h"
#include "VulkanOutput.h"
#include "VulkanDevice.h"
#include "VulkanRenderEngine/VKWrapper/VKSemaphore.h"
#include "VulkanRenderEngine/VKWrapper/VKFence.h"

class VKWrapper::VKFence;

namespace VKCore
{

	class VulkanSwapchain
	{
	public:
		VulkanSwapchain() = default;
		~VulkanSwapchain();

		// 禁止拷贝，允许移动
		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
		VulkanSwapchain(VulkanSwapchain&& other) noexcept;
		VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;

		void Release();

		vk::Result Create(
			std::shared_ptr<VulkanDevice> device,
			std::shared_ptr<VulkanSurface> surface,
			const VkExtent2D& windowSize,
			uint32_t targetImageCount,
			bool limitFrameRate,
			vk::SwapchainCreateFlagsKHR flags = {}
		);

		vk::Format GetImageFormat() const;
		vk::ColorSpaceKHR GetImageColorSpace() const;
		uint32_t GetCurrentImageIndex() const;
		uint32_t GetSwapchainImageCount() const;
		const std::vector<vk::ImageView>& SwapchainImageView() const;
		const std::vector<vk::Image>& SwapchainImage() const;


		vk::Result SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable, uint32_t& outImageIndex);
		vk::Result SwapImage(const VKWrapper::VKFence& fence_imageIsAvailable, uint32_t& outImageIndex);
		vk::Result SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable, const VKWrapper::VKFence& fence_imageIsAvailable, uint32_t& outImageIndex);
		vk::Result PresentImage(const VKWrapper::VKSemaphore& semaphore_renderingIsOver, uint32_t imageIndex);

		void AddCallback_CreateSwapchain(std::function<void()> func);
		void AddCallback_DestroySwapchain(std::function<void()> func);

	private:
		vk::Result Create_Internal();
		vk::Result RecreateSwapchain();

		vk::Result PresentImage(vk::PresentInfoKHR& presentInfo);

	private:
		std::weak_ptr<VulkanDevice> m_device;
		std::weak_ptr<VulkanSurface> m_surface;

		vk::SwapchainKHR m_swapchain = VK_NULL_HANDLE;
		vk::SwapchainCreateInfoKHR m_swapchainCreateInfo;

		std::vector<vk::Image> m_swapchainImages;
		std::vector<vk::ImageView> m_swapchainImageViews;

		std::vector<std::function<void()>> m_callbacks_createSwapchain;
		std::vector<std::function<void()>> m_callbacks_destroySwapchain;
	};

}