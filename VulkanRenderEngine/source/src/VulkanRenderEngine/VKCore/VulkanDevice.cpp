#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"

static void ExecuteCallbacks(std::vector<std::function<void()>>& callbacks)
{
	for (auto& callback : callbacks)
	{
		if (callback)
			callback();
	}
}

using namespace VKCore;


static void NeedDeviceProc(VkDevice device) {
	static bool loaded = false;
	static std::once_flag flag;

	if (loaded || device == VK_NULL_HANDLE)
		return;

	auto loadfunction = [&]-> void {
		volkLoadDevice(device);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(device));
		loaded = true;
		};
	std::call_once(flag, loadfunction);
}


VKCore::VulkanDevice::VulkanDevice(std::shared_ptr<VulkanInstance> instance, VulkanPhysicalDeviceInfo& info, vk::DeviceCreateFlags flags)
{
	Create(instance, info, flags);
}

VulkanDevice::~VulkanDevice()
{
	Release();
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
{
	m_instance = other.m_instance;
	m_physicalDeviceInfo = other.m_physicalDeviceInfo;
	m_device = other.m_device;
	m_allocator = other.m_allocator;;
	m_queue_graphics = other.m_queue_graphics;;
	m_queue_presentation = other.m_queue_presentation;;
	m_queue_compute = other.m_queue_compute;;
	m_deviceExtensions = other.m_deviceExtensions;;
	m_callbacks_createDevice = other.m_callbacks_createDevice;;
	m_callbacks_destroyDevice = other.m_callbacks_destroyDevice;;

	other.m_instance.reset();
	other.m_physicalDeviceInfo = {};
	other.m_device = VK_NULL_HANDLE;
	other.m_allocator = VK_NULL_HANDLE;
	other.m_queue_graphics = VK_NULL_HANDLE;
	other.m_queue_presentation = VK_NULL_HANDLE;
	other.m_queue_compute = VK_NULL_HANDLE;
	other.m_deviceExtensions.clear();
	other.m_callbacks_createDevice.clear();
	other.m_callbacks_destroyDevice.clear();
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept
{
	if (this == &other)
		return *this;

	m_instance = other.m_instance;
	m_physicalDeviceInfo = other.m_physicalDeviceInfo;
	m_device = other.m_device;
	m_allocator = other.m_allocator;;
	m_queue_graphics = other.m_queue_graphics;;
	m_queue_presentation = other.m_queue_presentation;;
	m_queue_compute = other.m_queue_compute;;
	m_deviceExtensions = other.m_deviceExtensions;;
	m_callbacks_createDevice = other.m_callbacks_createDevice;;
	m_callbacks_destroyDevice = other.m_callbacks_destroyDevice;;

	other.m_instance.reset();
	other.m_physicalDeviceInfo = {};
	other.m_device = VK_NULL_HANDLE;
	other.m_allocator = VK_NULL_HANDLE;
	other.m_queue_graphics = VK_NULL_HANDLE;
	other.m_queue_presentation = VK_NULL_HANDLE;
	other.m_queue_compute = VK_NULL_HANDLE;
	other.m_deviceExtensions.clear();
	other.m_callbacks_createDevice.clear();
	other.m_callbacks_destroyDevice.clear();

	return *this;
}

void VulkanDevice::Release()
{

	if (m_allocator)
	{
		vmaDestroyAllocator(m_allocator);
		m_allocator = VK_NULL_HANDLE;
	}

	if (m_device) {
		WaitIdle();
		ExecuteCallbacks(m_callbacks_destroyDevice);
		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
		m_deviceExtensions.clear();
		m_instance.reset();
		m_physicalDeviceInfo = {};
		m_queue_graphics = VK_NULL_HANDLE;
		m_queue_presentation = VK_NULL_HANDLE;
		m_queue_compute = VK_NULL_HANDLE;
		m_callbacks_createDevice.clear();
		m_callbacks_destroyDevice.clear();
	}

}


vk::Result VulkanDevice::Create(
	std::shared_ptr<VulkanInstance> instance,
	VulkanPhysicalDeviceInfo& info,
	vk::DeviceCreateFlags flags
)
{
	Release();

	if (!instance || info.physicalDevice == VK_NULL_HANDLE)
		return vk::Result::eErrorInitializationFailed;

	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;

	auto addQueueFamily = [&](uint32_t familyIndex) {
		if (familyIndex == VK_QUEUE_FAMILY_IGNORED) return;

		for (const auto& existing : queueCreateInfos) {
			if (existing.queueFamilyIndex == familyIndex) {
				return;
			}
		}

		vk::DeviceQueueCreateInfo createInfo;
		createInfo.setQueueFamilyIndex(familyIndex)
			.setQueueCount(1)
			.setPQueuePriorities(&queuePriority);
		queueCreateInfos.push_back(createInfo);
		};

	// 按优先级顺序添加（先图形、再呈现、再计算）
	addQueueFamily(info.graphicsQueueFamily);
	addQueueFamily(info.presentQueueFamily);
	addQueueFamily(info.computeQueueFamily);


	m_deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

	std::vector<const char*> deviceExtensions;
	for (auto& e : m_deviceExtensions)
		deviceExtensions.push_back(e.c_str());

	using ChainType = vk::StructureChain<
		vk::PhysicalDeviceFeatures2, 
		vk::PhysicalDeviceVulkan11Features, 
		vk::PhysicalDeviceVulkan12Features, 
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceVulkan14Features,
		//vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR
	>;

	ChainType chain =
		info.physicalDevice.getFeatures2<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan11Features, 
		vk::PhysicalDeviceVulkan12Features, 
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceVulkan14Features,
		//vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR
		>();

	auto& deviceFeatures2 = chain.get<vk::PhysicalDeviceFeatures2>();
	auto& features11 = chain.get<vk::PhysicalDeviceVulkan11Features>();
	auto& features12 = chain.get<vk::PhysicalDeviceVulkan12Features>();
	auto& features13 = chain.get<vk::PhysicalDeviceVulkan13Features>();
	auto& features14 = chain.get<vk::PhysicalDeviceVulkan14Features>();

	//auto& rayTraceExt = chain.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT>();
	//if (!rayTraceExt.rayTracingInvocationReorder)
	//	std::cerr << std::format("rayTracingInvocationReorder not support!\n");

	if (!features12.descriptorBindingPartiallyBound ||
		!features12.runtimeDescriptorArray ||
		!features12.descriptorBindingSampledImageUpdateAfterBind ||
		!features12.descriptorBindingStorageBufferUpdateAfterBind ||
		!features12.descriptorBindingStorageImageUpdateAfterBind ||
		!features12.descriptorBindingStorageTexelBufferUpdateAfterBind ||
		!features12.descriptorBindingUniformBufferUpdateAfterBind ||
		!features12.descriptorBindingUniformTexelBufferUpdateAfterBind ||
		!features12.shaderSampledImageArrayNonUniformIndexing
		) {
		std::cerr << std::format("some indexingFeatures not support!\n");
	}

	if (!features13.shaderDemoteToHelperInvocation)
		std::cerr << std::format("shaderDemoteToHelperInvocation not support!\n");


	vk::DeviceCreateInfo deviceCreateInfo;
	deviceCreateInfo.setFlags(flags)
		.setQueueCreateInfos(queueCreateInfos)
		.setPEnabledExtensionNames(deviceExtensions)
		.setPNext(&deviceFeatures2);

	auto [result, device] = info.physicalDevice.createDevice(deviceCreateInfo);
	if (result != vk::Result::eSuccess) {
		std::cout << std::format("[ VulkanDevice ] ERROR\nFailed to create a vulkan logical device!\nError code: {}\n", to_string(result));
		return result;
	}
	else
	{
		m_device = device;
		m_physicalDeviceInfo = info;
		NeedDeviceProc(VkDevice(device));
	}

	if (info.physicalDeviceProperties.properties.limits.maxViewports < 16)
		std::cout << std::format("[ VulkanDevice Warning ] PhysicalDeviceLimits maxViewports < 16 , count = {}", info.physicalDeviceProperties.properties.limits.maxViewports);

	if (info.graphicsQueueFamily != VK_QUEUE_FAMILY_IGNORED)
		m_queue_graphics = m_device.getQueue(info.graphicsQueueFamily, 0);
	if (info.presentQueueFamily != VK_QUEUE_FAMILY_IGNORED)
		m_queue_presentation = m_device.getQueue(info.presentQueueFamily, 0);
	if (info.computeQueueFamily != VK_QUEUE_FAMILY_IGNORED)
		m_queue_compute = m_device.getQueue(info.computeQueueFamily, 0);

	m_instance = instance;

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.instance = instance->GetHandle();
	allocatorInfo.physicalDevice = info.physicalDevice;
	allocatorInfo.device = m_device;
	allocatorInfo.vulkanApiVersion = instance->GetApiVersion();
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

	VmaVulkanFunctions vkFuncs = {};
	vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vkFuncs);
	allocatorInfo.pVulkanFunctions = &vkFuncs;

	if (vk::Result result = static_cast<vk::Result>(vmaCreateAllocator(&allocatorInfo, &m_allocator)); result != vk::Result::eSuccess) {
		std::cout << std::format("[ VulkanDevice ] ERROR\nFailed to create a vmaAllocator !\nError code: {}\n", vk::to_string(result));
		return result;
	}

	std::cout << std::format("[ VulkanDevice ] use physical Device: {}\n", std::string(info.physicalDeviceProperties.properties.deviceName.data()));
	ExecuteCallbacks(m_callbacks_createDevice);
	return vk::Result::eSuccess;
}

// ---------- Getter ----------
vk::Device VulkanDevice::GetHandle() const { return m_device; }

VmaAllocator VulkanDevice::GetAllocator() const { return m_allocator; }

vk::PhysicalDevice VulkanDevice::GetPhysicalDevice() const { return m_physicalDeviceInfo.physicalDevice; }

const VulkanPhysicalDeviceInfo& VKCore::VulkanDevice::GetVulkanPhysicalDeviceInfo() const { return m_physicalDeviceInfo; }

const vk::PhysicalDeviceProperties2& VulkanDevice::GetPhysicalDeviceProperties() const { return m_physicalDeviceInfo.physicalDeviceProperties; }

const vk::PhysicalDeviceMemoryProperties& VulkanDevice::GetPhysicalDeviceMemoryProperties() const { return m_physicalDeviceInfo.physicalDeviceMemoryProperties; }

std::weak_ptr<VulkanInstance> VulkanDevice::GetInstance() const { return m_instance; }

uint32_t VulkanDevice::GetGraphicsQueueFamily() const { return m_physicalDeviceInfo.graphicsQueueFamily; }

uint32_t VulkanDevice::GetPresentQueueFamily() const { return m_physicalDeviceInfo.presentQueueFamily; }

uint32_t VulkanDevice::GetComputeQueueFamily() const { return m_physicalDeviceInfo.computeQueueFamily; }

vk::Queue VulkanDevice::GetGraphicsQueue() const { return m_queue_graphics; }

vk::Queue VulkanDevice::GetPresentQueue() const { return m_queue_presentation; }

vk::Queue VulkanDevice::GetComputeQueue() const { return m_queue_compute; }

SpinLock& VKCore::VulkanDevice::GetGraphicsQueueMutex()
{
	return m_queue_graphics_mutex;
}

SpinLock& VKCore::VulkanDevice::GetPresentQueueMutex()
{
	return m_queue_presentation_mutex;
}

SpinLock& VKCore::VulkanDevice::GetComputeQueueMutex()
{
	return m_queue_compute_mutex;
}

const std::vector<std::string>& VulkanDevice::GetDeviceExtensions() const { return m_deviceExtensions; }

void VulkanDevice::AddDeviceExtension(const std::string& extensionName) { m_deviceExtensions.push_back(extensionName); }

void VulkanDevice::AddCallback_CreateDevice(std::function<void()> func) {
	m_callbacks_createDevice.push_back(func);
}

void VulkanDevice::AddCallback_DestroyDevice(std::function<void()> func) {
	m_callbacks_destroyDevice.push_back(func);
}

VkResult VulkanDevice::WaitIdle() const
{
	VkResult result = vkDeviceWaitIdle(m_device);
	if (result)
		std::cout << std::format("[ graphicsBase ] ERROR\nFailed to wait for the device to be idle!\nError code: {}\n", string_VkResult(result));
	return result;
}


