#pragma once

#include "vkstdafx.h"
#include "./BaseVKImage.h"
#include "VulkanRenderEngine/SharedTexture.h"

namespace VKWrapper {

	class SharedImage :public BaseVKImage
	{

	public:
		SharedImage() = default;
		~SharedImage();

		SharedImage(const SharedImage&) = delete;
		SharedImage& operator=(const SharedImage&) = delete;

		bool Create(VKCore::VulkanDevice* device, std::shared_ptr<SharedTexture> sharedTexture, vk::Format& outFormat);
		virtual void Release();

	private:
		vk::DeviceMemory m_devicememory;
	};

} // namespace VKWrapper
