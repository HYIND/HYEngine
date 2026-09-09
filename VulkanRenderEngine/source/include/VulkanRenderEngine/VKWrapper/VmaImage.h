#pragma once

#include "vkstdafx.h"
#include "./BaseVKImage.h"

namespace VKWrapper {

	class VmaImage :public BaseVKImage
	{
	public:
		enum class ImageType { Image2D = 0, ImageCube };

	public:
		VmaImage() = default;
		virtual ~VmaImage();

		VmaImage(const VmaImage&) = delete;
		VmaImage& operator=(const VmaImage&) = delete;

		bool Create(VKCore::VulkanDevice* device, vk::Format format, vk::Extent2D size, uint32_t mipLevels, ImageType type = ImageType::Image2D, bool cpuAccess = false);
		bool Create(VKCore::VulkanDevice* device, const vk::ImageCreateInfo& imageInfo, const VmaAllocationCreateInfo& allocInfo);
		void Release();

	private:
		VmaAllocator m_allocator = VK_NULL_HANDLE;
		VmaAllocation m_allocation = VK_NULL_HANDLE;
	};

} // namespace VKWrapper
