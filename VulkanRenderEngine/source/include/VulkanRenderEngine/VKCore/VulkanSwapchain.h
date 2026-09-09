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
			bool limitFrameRate,
			vk::SwapchainCreateFlagsKHR flags = {});

		vk::Format GetImageFormat() const;
		vk::ColorSpaceKHR GetImageColorSpace() const;
		uint32_t GetCurrentImageIndex() const;
		uint32_t GetSwapchainImageCount() const;
		const std::vector<vk::ImageView>& SwapchainImageView() const;
		const std::vector<vk::Image>& SwapchainImage() const;


		//该函数用于获取交换链图像索引到currentImageIndex，以及在需要重建交换链时调用RecreateSwapchain()、重建交换链后销毁旧交换链
		vk::Result SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable);
		vk::Result SwapImage(const VKWrapper::VKFence& fence_imageIsAvailable);
		vk::Result SwapImage(const VKWrapper::VKSemaphore& semaphore_imageIsAvailable, const VKWrapper::VKFence& fence_imageIsAvailable);
		vk::Result PresentImage(vk::PresentInfoKHR& presentInfo);
		vk::Result PresentImage(const VKWrapper::VKSemaphore& semaphore_renderingIsOver);//该函数用于在渲染循环中呈现图像的常见情形

		void AddCallback_CreateSwapchain(std::function<void()> func);
		void AddCallback_DestroySwapchain(std::function<void()> func);

	private:
		vk::Result Create_Internal();
		vk::Result RecreateSwapchain();

	private:
		std::weak_ptr<VulkanDevice> m_device;
		std::weak_ptr<VulkanSurface> m_surface;

		vk::SwapchainKHR m_swapchain = VK_NULL_HANDLE;
		vk::SwapchainCreateInfoKHR m_swapchainCreateInfo;

		std::vector<vk::Image> m_swapchainImages;
		std::vector<vk::ImageView> m_swapchainImageViews;
		uint32_t m_currentImageIndex = 0;

		std::vector<std::function<void()>> m_callbacks_createSwapchain;
		std::vector<std::function<void()>> m_callbacks_destroySwapchain;
	};

}