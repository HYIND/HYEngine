#pragma once
#include "vkstdafx.h"
#include "VulkanInstance.h"
#include "SpinLock.h"

namespace VKCore
{

	class VulkanDevice
	{
	public:
		VulkanDevice() = default;
		VulkanDevice(std::shared_ptr<VulkanInstance> instance, VulkanPhysicalDeviceInfo& info, vk::DeviceCreateFlags flags = {});
		~VulkanDevice();

		// 禁止拷贝，允许移动
		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;
		VulkanDevice(VulkanDevice&& other) noexcept;
		VulkanDevice& operator=(VulkanDevice&& other) noexcept;

		void Release();

		vk::Result Create(
			std::shared_ptr<VulkanInstance> instance,
			VulkanPhysicalDeviceInfo& info,
			vk::DeviceCreateFlags flags = {}
		);

		// ---------- Getter ----------
		vk::Device GetHandle() const;
		VmaAllocator GetAllocator() const;
		vk::PhysicalDevice GetPhysicalDevice() const;
		const vk::PhysicalDeviceProperties& GetPhysicalDeviceProperties() const;
		const vk::PhysicalDeviceMemoryProperties& GetPhysicalDeviceMemoryProperties() const;

		std::weak_ptr<VulkanInstance> GetInstance() const;

		uint32_t GetGraphicsQueueFamily() const;
		uint32_t GetPresentQueueFamily() const;
		uint32_t GetComputeQueueFamily() const;

		vk::Queue GetGraphicsQueue() const;
		vk::Queue GetPresentQueue() const;
		vk::Queue GetComputeQueue() const;

		SpinLock& GetGraphicsQueueMutex();
		SpinLock& GetPresentQueueMutex();
		SpinLock& GetComputeQueueMutex();

		const std::vector<std::string>& GetDeviceExtensions() const;

		void AddDeviceExtension(const std::string& extensionName);

		void AddCallback_CreateDevice(std::function<void()> func);
		void AddCallback_DestroyDevice(std::function<void()> func);

		VkResult WaitIdle() const;

	private:
		std::weak_ptr<VulkanInstance> m_instance;
		VulkanPhysicalDeviceInfo m_physicalDeviceInfo = {};

		vk::Device m_device = VK_NULL_HANDLE;
		VmaAllocator m_allocator = VK_NULL_HANDLE;

		vk::Queue m_queue_graphics = VK_NULL_HANDLE;
		vk::Queue m_queue_presentation = VK_NULL_HANDLE;
		vk::Queue m_queue_compute = VK_NULL_HANDLE;

		std::vector<std::string> m_deviceExtensions;

		std::vector<std::function<void()>> m_callbacks_createDevice;
		std::vector<std::function<void()>> m_callbacks_destroyDevice;

		SpinLock m_queue_graphics_mutex;
		SpinLock m_queue_presentation_mutex;
		SpinLock m_queue_compute_mutex;
	};

}