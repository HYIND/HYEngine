#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"

namespace VKWrapper
{
	class VKSemaphore
	{
	public:
		vk::Semaphore GetHandle() const;
		bool IsTimeline() const;
		void Release();

	protected:
		VKSemaphore() = default;

		VKCore::VulkanDevice* _device = nullptr;
		vk::Semaphore _handle = VK_NULL_HANDLE;
		bool _isTimeline = false;
	};

	class VKBinarySemaphore : public VKSemaphore
	{
	public:
		VKBinarySemaphore(VKCore::VulkanDevice* device);
		VKBinarySemaphore(VKBinarySemaphore&& other) noexcept;
		VKBinarySemaphore& operator=(VKBinarySemaphore&& other) noexcept;
		~VKBinarySemaphore();

		vk::Result Create(VKCore::VulkanDevice* device);
	};

	class VKTimelineSemaphore : public VKSemaphore
	{
	public:
		VKTimelineSemaphore(VKCore::VulkanDevice* device, const uint64_t initialValue = 0);
		VKTimelineSemaphore(VKTimelineSemaphore&& other) noexcept;
		VKTimelineSemaphore& operator=(VKTimelineSemaphore&& other) noexcept;
		~VKTimelineSemaphore();

		vk::Result Create(VKCore::VulkanDevice* device, const uint64_t initialValue = 0);

		bool Wait(uint64_t targetValue)
		{
			vk::SemaphoreWaitInfo waitInfo;
			waitInfo
				.setSemaphores(_handle)
				.setValues(targetValue);

			vk::Result result = _device->GetHandle().waitSemaphores(waitInfo, UINT64_MAX);
			if (result != vk::Result::eSuccess) {
				std::cerr << std::format("[ VKTimelineSemaphore ] Error when Wait Semaphore! Error : {}\n", to_string(result));
				return false;
			}
			return true;
		}
	};
}