#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"

namespace VKWrapper
{
	class VKSemaphore
	{
	public:
		VKSemaphore(VKCore::VulkanDevice* device, const vk::SemaphoreCreateInfo& createInfo = {});
		VKSemaphore(VKSemaphore&& other) noexcept;
		VKSemaphore& operator=(VKSemaphore&& other) noexcept;
		~VKSemaphore();

		vk::Result Create(VKCore::VulkanDevice* device, const vk::SemaphoreCreateInfo& createInfo = {});
		void Release();

		vk::Semaphore GetHandle() const;

	private:
		VKCore::VulkanDevice* _device = nullptr;
		vk::Semaphore _handle = VK_NULL_HANDLE;
	};
}