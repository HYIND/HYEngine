#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"

namespace VKWrapper
{

	class VKImageView
	{
	public:
		VKImageView() = default;
		VKImageView(VKCore::VulkanDevice* device, const vk::ImageViewCreateInfo& createInfo);
		VKImageView(VKImageView&& other) noexcept;
		VKImageView& operator=(VKImageView&& other) noexcept;
		~VKImageView();

		vk::Result Create(VKCore::VulkanDevice* device, const vk::ImageViewCreateInfo& createInfo);
		void Release();

		vk::ImageView GetHandle() const;;

	private:
		VKCore::VulkanDevice* _device = nullptr;
		vk::ImageView _handle = VK_NULL_HANDLE;
	};
}