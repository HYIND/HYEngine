#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"

namespace VKWrapper
{

	class VKFence
	{
	public:
		// 默认创建未置位的栅栏
		VKFence(VKCore::VulkanDevice* device, const vk::FenceCreateInfo& createInfo = {});
		VKFence(VKFence&& other) noexcept;
		VKFence& operator=(VKFence&& other) noexcept;
		~VKFence();

		vk::Result Create(VKCore::VulkanDevice* device, const vk::FenceCreateInfo& createInfo = {});
		void Release();

		vk::Fence GetHandle() const;

		vk::Result Wait() const;
		vk::Result Reset() const;
		vk::Result WaitAndReset() const;
		vk::Result Status() const;

	private:
		VKCore::VulkanDevice* _device = nullptr;
		vk::Fence _handle = VK_NULL_HANDLE;
	};
}