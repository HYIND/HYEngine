#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"

namespace VKWrapper
{

	class VKSampler
	{
	public:
		VKSampler() = default;
		VKSampler(VKCore::VulkanDevice* device, const vk::SamplerCreateInfo& createInfo);
		VKSampler(VKSampler&& other) noexcept;
		VKSampler& operator=(VKSampler&& other) noexcept;
		~VKSampler();

		vk::Result Create(VKCore::VulkanDevice* device, const vk::SamplerCreateInfo& createInfo);
		void Release();

		vk::Sampler GetHandle() const;;

	private:
		VKCore::VulkanDevice* _device = nullptr;
		vk::Sampler _handle = VK_NULL_HANDLE;
	};
}