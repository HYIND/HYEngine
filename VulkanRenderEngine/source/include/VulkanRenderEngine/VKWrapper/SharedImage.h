#pragma once

#include "vkstdafx.h"
#include "./BaseVKImage.h"
#include "VulkanRenderEngine/SharedTexture.h"

namespace VKWrapper {

	class SharedImage :public BaseVKImage
	{
	public:
		enum class ImageType { Image2D = 0, ImageCube };

	public:
		SharedImage() = default;
		~SharedImage();

		SharedImage(const SharedImage&) = delete;
		SharedImage& operator=(const SharedImage&) = delete;

		bool Create(VKCore::VulkanDevice* device, vk::Format format, vk::Extent2D size, uint32_t mipLevels, ImageType type = ImageType::Image2D, bool cpuAccess = false);
		bool Create(VKCore::VulkanDevice* device, const vk::ImageCreateInfo& imageInfo, const VmaAllocationCreateInfo& allocInfo);
		bool Create(VKCore::VulkanDevice* device, std::shared_ptr<SharedTexture> sharedTexture, vk::Format& outFormat);
		void Release();

	private:
		vk::DeviceMemory m_devicememory;
	};

} // namespace VKWrapper
