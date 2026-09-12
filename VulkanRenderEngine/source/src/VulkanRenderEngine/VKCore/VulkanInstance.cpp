#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanInstance.h"
#include "VulkanRenderEngine\GlobalConfig.h"

using namespace VKCore;


static void NeedInstanceProc(VkInstance instance) {
	static bool loaded = false;
	static std::once_flag flag;

	if (loaded || instance == VK_NULL_HANDLE)
		return;

	auto loadfunction = [&]-> void {
		volkLoadInstance(instance);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance));
		loaded = true;
		};
	std::call_once(flag, loadfunction);
}


void VulkanInstanceConfig::EnableValidation() {
	instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
	instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

void VulkanInstanceConfig::AddInstanceLayer(const std::string& layerName) {
	instanceLayers.push_back(layerName);
}

void VulkanInstanceConfig::AddInstanceExtension(const std::string& extensionName) {
	instanceExtensions.push_back(extensionName);
}

VulkanInstance::~VulkanInstance()
{
	Release();
}

void VulkanInstance::Release()
{
	if (m_debugMessenger)
	{
		m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
		m_debugMessenger = VK_NULL_HANDLE;
	}

	if (m_instance)
	{
		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;

		m_config = {};
		m_availablePhysicalDevices.clear();
	}
}

vk::Result VulkanInstance::CreateInstance(vk::InstanceCreateFlags flags)
{
	Release();

#ifdef Enable_Vulkan_Validation
	m_config.EnableValidation();
#endif

	std::vector<const char*> layerNames;
	layerNames.reserve(m_config.instanceLayers.size());
	for (const auto& layer : m_config.instanceLayers)
		layerNames.push_back(layer.c_str());

	std::vector<const char*> extensionsNames;
	extensionsNames.reserve(m_config.instanceExtensions.size());
	for (const auto& layer : m_config.instanceExtensions)
		extensionsNames.push_back(layer.c_str());

	vk::ApplicationInfo applicationInfo;
	applicationInfo.setApiVersion(m_config.apiVersion);

	vk::InstanceCreateInfo instanceCreateInfo;
	instanceCreateInfo.setFlags(flags)
		.setPApplicationInfo(&applicationInfo)
		.setEnabledLayerCount(m_config.instanceLayers.size())
		.setPpEnabledLayerNames(layerNames.data())
		.setEnabledExtensionCount(m_config.instanceExtensions.size())
		.setPpEnabledExtensionNames(extensionsNames.data());

	auto [result, instance] = vk::createInstance(instanceCreateInfo);
	if (result == vk::Result::eSuccess)
	{
		m_instance = instance;
		NeedInstanceProc(VkInstance(instance));
	}
	else
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to create a vulkan instance!\nError code: {}\n", vk::to_string(result));
		return result;
	}


	//成功创建Vulkan实例后，输出Vulkan版本
	std::cout << std::format(
		"Vulkan API Version: {}.{}.{}\n",
		VK_API_VERSION_MAJOR(m_config.apiVersion),
		VK_API_VERSION_MINOR(m_config.apiVersion),
		VK_API_VERSION_PATCH(m_config.apiVersion));


	auto needDebugMessger = [&]()->bool
		{
			static std::string layerExt = "VK_LAYER_KHRONOS_validation";
			static std::string instanceExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

			bool find = false;
			for (auto& ext : m_config.instanceLayers)
			{
				if (ext == "VK_LAYER_KHRONOS_validation")
				{
					find = true;
					break;
				}
			}
			if (!find)
				return false;

			find = false;
			for (auto& ext : m_config.instanceExtensions)
			{
				if (ext == VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
				{
					find = true;
					break;
				}
			}

			return find;
		};

	if (needDebugMessger())
	{
		if (auto result = CreateDebugMessenger(); result != vk::Result::eSuccess)
		{
			std::cout << std::format("[ graphicsBase ] ERROR\nFailed to CreateDebugMessenger !\nError code: {}\n", vk::to_string(result));
			return result;
		}
	}

	if (auto result = GetPhysicalDevices(); result != vk::Result::eSuccess)
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to GetPhysicalDevices !\nError code: {}\n", vk::to_string(result));
		return result;
	}

	return vk::Result::eSuccess;
}

vk::Result VulkanInstance::CreateDebugMessenger()
{
	static PFN_vkDebugUtilsMessengerCallbackEXT DebugUtilsMessengerCallback = [](
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageTypes,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)->VkBool32 {
			std::cout << std::format("{}\n\n", pCallbackData->pMessage);
			return VK_FALSE;
		};

	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo;
	debugUtilsMessengerCreateInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
		.setPfnUserCallback(DebugUtilsMessengerCallback);

	auto [result, debugMessenger] = m_instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfo);
	if (result == vk::Result::eSuccess)
		m_debugMessenger = debugMessenger;
	else
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to create a debug messenger!\nError code: {}\n", vk::to_string(result));
		return result;
	}

	return vk::Result::eSuccess;
}

const std::vector<vk::PhysicalDevice> VulkanInstance::GetAvailablePhysicalDevice() const {
	return m_availablePhysicalDevices;
}

uint32_t VulkanInstance::GetAvailablePhysicalDeviceCount() const {
	return uint32_t(m_availablePhysicalDevices.size());
}

static void GetPhysicalDeviceProp(VulkanPhysicalDeviceInfo& info)
{
	if (GlobalConfig::RTCoreEnable)
	{
		using ChainType = vk::StructureChain<
			vk::PhysicalDeviceProperties2,
			vk::PhysicalDeviceAccelerationStructurePropertiesKHR,
			vk::PhysicalDeviceRayTracingPipelinePropertiesKHR
		>;

		ChainType chain =
			info.physicalDevice.getProperties2<
			vk::PhysicalDeviceProperties2,
			vk::PhysicalDeviceAccelerationStructurePropertiesKHR,
			vk::PhysicalDeviceRayTracingPipelinePropertiesKHR
			>();

		info.physicalDeviceProperties = chain.get<vk::PhysicalDeviceProperties2>();
		info.accelProps = chain.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
		info.rtPipelineProps = chain.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
	}
	else
		info.physicalDeviceProperties = info.physicalDevice.getProperties2();

	info.physicalDeviceMemoryProperties = info.physicalDevice.getMemoryProperties();
}

bool VulkanInstance::GetSuitablePhysicalDevice(
	VulkanPhysicalDeviceInfo& info,
	bool enableGraphicsQueue, bool enablePresentQueue, bool enableComputeQueue,
	VkSurfaceKHR surface
)
{

	for (auto device : m_availablePhysicalDevices)
	{
		uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t presentFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;

		vk::Result result = GetQueueFamilyIndices(
			device,
			enableGraphicsQueue,
			enablePresentQueue,
			enableComputeQueue,
			graphicsFamily,
			presentFamily,
			computeFamily,
			surface
		);

		if (result != vk::Result::eSuccess)
			continue;

		if (
			(enableGraphicsQueue && graphicsFamily == VK_QUEUE_FAMILY_IGNORED) ||
			(enablePresentQueue && presentFamily == VK_QUEUE_FAMILY_IGNORED) ||
			(enableComputeQueue && computeFamily == VK_QUEUE_FAMILY_IGNORED)
			)
			continue;

		info.physicalDevice = device;
		info.graphicsQueueFamily = graphicsFamily;
		info.presentQueueFamily = presentFamily;
		info.computeQueueFamily = computeFamily;

		GetPhysicalDeviceProp(info);

		return true;
	}

	return false;
}

bool VulkanInstance::GetSuitablePhysicalDevice(
	VulkanPhysicalDeviceInfo& info,
	bool enableGraphicsQueue, bool enableComputeQueue
)
{

	for (auto device : m_availablePhysicalDevices)
	{
		uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t presentFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t computeFamily = VK_QUEUE_FAMILY_IGNORED;

		vk::Result result = GetQueueFamilyIndices(
			device,
			enableGraphicsQueue,
			false,
			enableComputeQueue,
			graphicsFamily,
			presentFamily,
			computeFamily,
			VK_NULL_HANDLE
		);

		if (result != vk::Result::eSuccess)
			continue;

		if (
			(enableGraphicsQueue && graphicsFamily == VK_QUEUE_FAMILY_IGNORED) ||
			(enableComputeQueue && computeFamily == VK_QUEUE_FAMILY_IGNORED)
			)
			continue;


		info.physicalDevice = device;
		info.graphicsQueueFamily = graphicsFamily;
		info.presentQueueFamily = presentFamily;
		info.computeQueueFamily = computeFamily;

		GetPhysicalDeviceProp(info);

		return true;
	}

	return false;
}


vk::Result VulkanInstance::GetQueueFamilyIndices(
	vk::PhysicalDevice physicalDevice,
	bool enableGraphicsQueue, bool enablePresentQueue, bool enableComputeQueue,
	uint32_t& outGraphicsFamily, uint32_t& outPresentFamily, uint32_t& outComputeFamily,
	VkSurfaceKHR surface
)
{
	// 初始化输出
	outGraphicsFamily = VK_QUEUE_FAMILY_IGNORED;
	outPresentFamily = VK_QUEUE_FAMILY_IGNORED;
	outComputeFamily = VK_QUEUE_FAMILY_IGNORED;

	// 获取队列族
	auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
	if (queueFamilyProperties.empty()) {
		return vk::Result::eErrorInitializationFailed;
	}

	// 遍历所有队列族
	for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
		const auto& props = queueFamilyProperties[i];

		// 查找图形队列
		if (enableGraphicsQueue &&
			outGraphicsFamily == VK_QUEUE_FAMILY_IGNORED &&
			(props.queueFlags & vk::QueueFlagBits::eGraphics)) {
			outGraphicsFamily = i;
		}

		// 查找计算队列
		if (enableComputeQueue &&
			outComputeFamily == VK_QUEUE_FAMILY_IGNORED &&
			(props.queueFlags & vk::QueueFlagBits::eCompute)) {
			outComputeFamily = i;
		}

		// 查找呈现队列
		if (enablePresentQueue &&
			outPresentFamily == VK_QUEUE_FAMILY_IGNORED &&
			surface != VK_NULL_HANDLE) {
			auto [result, supportsPresent] = physicalDevice.getSurfaceSupportKHR(i, surface);
			if (supportsPresent == VK_TRUE) {
				outPresentFamily = i;
			}
		}
	}

	return vk::Result::eSuccess;
}

vk::Result VulkanInstance::GetPhysicalDevices()
{
	m_availablePhysicalDevices.clear();

	auto [result, phydevices] = m_instance.enumeratePhysicalDevices();
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to get the count of physical devices!\nError code: {}\n", vk::to_string(result));
		return result;
	}
	if (phydevices.empty())
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to find any physical device supports vulkan!\n");
		abort();
	}
	m_availablePhysicalDevices = std::move(phydevices);
	return vk::Result::eSuccess;
}

vk::Instance VulkanInstance::GetHandle() const {
	return m_instance;
}

vk::Result VulkanInstance::SetLatestApiVersion() {
	uint32_t apiVersion = VK_API_VERSION_1_0;
	auto [result, version] = vk::enumerateInstanceVersion();
	if (result == vk::Result::eSuccess)
	{
		apiVersion = version;
	}
	else
	{
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to get vulkan version!\nError code: {}\n", vk::to_string(result));
		return result;
	}

	if (apiVersion > m_config.apiVersion)
		m_config.apiVersion = apiVersion;
	return vk::Result::eSuccess;
}

void VulkanInstance::AddInstanceLayer(const std::string& layerName) {
	m_config.AddInstanceLayer(layerName);
}

void VulkanInstance::AddInstanceExtension(const std::string& extensionName) {
	m_config.AddInstanceExtension(extensionName);
}

uint32_t VulkanInstance::GetApiVersion() const {
	return m_config.apiVersion;
}

const std::vector<std::string>& VulkanInstance::GetInstanceLayers() const {
	return m_config.instanceLayers;
}

const std::vector<std::string>& VulkanInstance::GetInstanceExtensions() const {
	return m_config.instanceExtensions;
}

VulkanInstanceConfig VulkanInstance::GetConfig() const {
	return m_config;
}
