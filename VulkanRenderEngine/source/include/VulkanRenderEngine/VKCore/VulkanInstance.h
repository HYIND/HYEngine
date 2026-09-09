#pragma once
#include "vkstdafx.h"
#include <span>

namespace VKCore
{
	struct VulkanInstanceConfig
	{
		uint32_t apiVersion = VK_API_VERSION_1_0;
		std::vector<std::string> instanceLayers;
		std::vector<std::string> instanceExtensions;

		void EnableValidation();
		void AddInstanceLayer(const std::string& layerName);
		void AddInstanceExtension(const std::string& extensionName);
	};

	struct VulkanPhysicalDeviceInfo {
		vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
		vk::PhysicalDeviceProperties physicalDeviceProperties;
		vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
		uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t presentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	};

	class VulkanInstance
	{
	public:
		VulkanInstance() = default;
		~VulkanInstance();

		// 禁止拷贝，允许移动
		VulkanInstance(const VulkanInstance&) = delete;
		VulkanInstance& operator=(const VulkanInstance&) = delete;
		VulkanInstance(VulkanInstance&& other) noexcept;
		VulkanInstance& operator=(VulkanInstance&& other) noexcept;

		void Release();

	public:
		vk::Result SetLatestApiVersion();
		void AddInstanceLayer(const std::string& layerName);
		void AddInstanceExtension(const std::string& extensionName);

		uint32_t GetApiVersion() const;
		const std::vector<std::string>& GetInstanceLayers() const;
		const std::vector<std::string>& GetInstanceExtensions() const;
		VulkanInstanceConfig GetConfig() const;

	public:
		vk::Result CreateInstance(vk::InstanceCreateFlags flags = {});
		vk::Instance GetHandle() const;

	public:
		const std::vector<vk::PhysicalDevice> GetAvailablePhysicalDevice() const;
		uint32_t GetAvailablePhysicalDeviceCount() const;

	public:
		// 查找可用物理设备，如需呈现队列，则需surface配合，查询物理设备是否可在surface上呈现
		bool GetSuitablePhysicalDevice(
			VulkanPhysicalDeviceInfo& info,
			bool enableGraphicsQueue, bool enablePresentQueue, bool enableComputeQueue,
			VkSurfaceKHR surface = VK_NULL_HANDLE
		);

		bool GetSuitablePhysicalDevice(
			VulkanPhysicalDeviceInfo& info,
			bool enableGraphicsQueue, bool enableComputeQueue
		);

	private:
		vk::Result GetQueueFamilyIndices(
			vk::PhysicalDevice physicalDevice,
			bool enableGraphicsQueue,
			bool enablePresentQueue,
			bool enableComputeQueue,
			uint32_t& outGraphicsFamily,
			uint32_t& outPresentFamily,
			uint32_t& outComputeFamily,
			VkSurfaceKHR surface
		);

		vk::Result GetPhysicalDevices();
		vk::Result CreateDebugMessenger();

	private:
		vk::Instance m_instance = VK_NULL_HANDLE;
		vk::DebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
		VulkanInstanceConfig m_config = {};

		std::vector<vk::PhysicalDevice> m_availablePhysicalDevices;
	};
}