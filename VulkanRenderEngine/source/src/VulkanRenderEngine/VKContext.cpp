#include "vkstdafx.h"
#include "VulkanRenderEngine/VKContext.h"
#include <algorithm>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>
#include "CriticalSectionLock.h"
#include "SpinLock.h"

thread_local VKThreadContext tls_context;

template<typename Lockable = RecursiveSpinLock>
static vk::Result SubmitCommandBuffer(
	vk::Queue queue,
	Lockable& queuemutex,
	const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& commandBuffers,
	const std::vector<WaitSemaphoreData>& waitSemaphores = {},
	const std::vector<SignalSemaphoreData>& signalSemaphores = {},
	const std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr
)
{
	std::vector<vk::CommandBuffer> cmdHandles;
	std::vector<vk::Semaphore> waitHandles;
	std::vector<vk::PipelineStageFlags> flags;
	std::vector<vk::Semaphore> signalHandles;

	std::vector<uint64_t> waitValues;
	std::vector<uint64_t> signalValues;

	auto fenceHandle = signalFence ? signalFence->GetHandle() : VK_NULL_HANDLE;

	// 只提交本次真正录过命令的句柄。
	// Pass 没东西可画时（比如场景里最后一个 mesh 被删了）也会照常 SubmitToQueue，
	// 这时句柄是从池子复用来的、没 Begin 过，里面还是上一次的旧命令，提交就会重放旧命令，
	// 引用到已经 Retire 销毁的 buffer。跳过它，信号量/fence 仍然照常提交，同步不受影响。
	for (auto& cmd : commandBuffers)
		if (cmd && cmd->GetHandle() && cmd->HasCommands())
			cmdHandles.push_back(cmd->GetHandle());

	bool hasTimeLine = false;

	if (!waitSemaphores.empty())
	{
		waitHandles.reserve(waitSemaphores.size());
		flags.reserve(waitSemaphores.size());
		waitValues.reserve(waitSemaphores.size());
		for (int i = 0; i < waitSemaphores.size(); i++)
		{
			auto& data = waitSemaphores[i];
			auto& semaphore = data.semaphore;
			waitHandles.push_back(semaphore ? semaphore->GetHandle() : VK_NULL_HANDLE);
			waitValues.push_back(data.value);
			flags.push_back(data.flags);

			if (!hasTimeLine && semaphore->IsTimeline())
				hasTimeLine = true;
		}
	}

	if (!signalSemaphores.empty())
	{
		signalHandles.reserve(signalSemaphores.size());
		signalValues.reserve(signalSemaphores.size());
		for (int i = 0; i < signalSemaphores.size(); i++)
		{
			auto& data = signalSemaphores[i];
			auto& semaphore = data.semaphore;
			signalHandles.push_back(semaphore ? semaphore->GetHandle() : VK_NULL_HANDLE);
			signalValues.push_back(data.value);

			if (!hasTimeLine && semaphore->IsTimeline())
				hasTimeLine = true;
		}
	}


	vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphores(waitHandles)
		.setWaitDstStageMask(flags)
		.setCommandBuffers(cmdHandles)
		.setSignalSemaphores(signalHandles);

	vk::TimelineSemaphoreSubmitInfo timelineInfo;
	if (hasTimeLine)
	{
		timelineInfo
			.setWaitSemaphoreValues(waitValues)
			.setSignalSemaphoreValues(signalValues);
		submitInfo.setPNext(&timelineInfo);
	}

	// 什么都没有（无命令、无信号量、无 fence）就不必进队列
	if (cmdHandles.empty() && waitHandles.empty() && signalHandles.empty() && !fenceHandle)
		return vk::Result::eSuccess;

	LockGuard guard(queuemutex);
	auto result = queue.submit(submitInfo, fenceHandle);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ SubmitCommandBuffer ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));
	return result;
}


std::shared_ptr<VKWrapper::VKCommandBuffer> VKContext::GetCommandBuffer()
{
	return tls_context.GetCommandBuffer();
}

bool VKContext::GetCommandBuffer(std::shared_ptr<VKWrapper::VKCommandBuffer> existedCmdInstance)
{
	return tls_context.GetCommandBuffer(existedCmdInstance);
}

vk::ResultValue<std::vector<vk::DescriptorSet>> VKContext::AllocateDescriptorSets(vk::DescriptorSetAllocateInfo& allocInfo) {
	allocInfo.setDescriptorPool(GetDescriptorPool());
	LockGuard guard(_descriptorPoolRequestMutex);
	return _device->GetHandle().allocateDescriptorSets(allocInfo);
}

vk::Result VKContext::FreeDescriptorSets(const std::vector<vk::DescriptorSet>& sets) {
	LockGuard guard(_descriptorPoolRequestMutex);
	return _device->GetHandle().freeDescriptorSets(GetDescriptorPool(), sets);
}

void VKContext::Retire(VKWrapper::IVKResource* res) {
	if (!res) return;
	LockGuard guard(_pendingDestoryResourceMutex);
	_pendingDestoryResource.push_back(PendingResource{ res, _frameIndex.load() });
}

void VKContext::ProcessRetireAndPushFrameIndex() {
	if (_pendingDestoryResource.empty())
		return;

	std::list<PendingResource> temp;
	uint32_t curFrame;

	{
		LockGuard guard(_pendingDestoryResourceMutex);
		curFrame = _frameIndex++;

		constexpr uint32_t clearGap = std::max(1u, GlobalConfig::MaxFramesInFlight) * 2;

		if (curFrame < clearGap)
			return;

		uint32_t safeFrame = curFrame - clearGap;

		// 找到第一个 frameIndex > safeFrame 的节点
		auto it = _pendingDestoryResource.begin();
		while (it != _pendingDestoryResource.end() && it->frameIndex <= safeFrame)
			++it;

		// 把 [begin, it) 切到 temp
		if (it != _pendingDestoryResource.begin()) {
			temp.splice(temp.begin(), _pendingDestoryResource,
				_pendingDestoryResource.begin(), it);
		}
	}

	for (auto& p : temp) {
		if (p.res)
		{
			p.res->Destroy();
			delete p.res;
		}
	}
}

std::shared_ptr<VKWrapper::VKCommandBuffer> VKThreadContext::GetCommandBuffer()
{
	NeedCommandPool();
	if (auto commandPool = _commandPool)
	{
		std::shared_ptr<VKWrapper::VKCommandBuffer> commandBuffer = std::make_shared<VKWrapper::VKCommandBuffer>();
		if (commandPool->AllocateBuffers(commandBuffer) == vk::Result::eSuccess)
			return commandBuffer;
	}
	return std::shared_ptr<VKWrapper::VKCommandBuffer>();
}

bool VKThreadContext::GetCommandBuffer(std::shared_ptr<VKWrapper::VKCommandBuffer> existedCmdInstance)
{
	if (!existedCmdInstance)
		return false;

	NeedCommandPool();
	if (auto commandPool = _commandPool)
	{
		if (commandPool->AllocateBuffers(existedCmdInstance) == vk::Result::eSuccess)
			return true;
	}
	return false;
}

std::shared_ptr<VKWrapper::VKCommandPool> VKThreadContext::GetCommandPool()
{
	NeedCommandPool();
	return _commandPool;
}

void VKThreadContext::NeedCommandPool()
{
	if (_commandPool)
		return;

	if (auto commandPool = std::make_shared<VKWrapper::VKCommandPool>(VKCONTEXT->GetDevice().get(), VKWrapper::VKCommandPool::QueueFamilyType::Graphics, vk::CommandPoolCreateFlagBits::eResetCommandBuffer))
		_commandPool = commandPool;
}

// ======================== 实例 / 设备 ========================

static void NeedVulkanBaseInitlized()
{
	static std::once_flag flag;
	std::call_once(flag, []() {
		vk::Result result = (vk::Result)volkInitialize();
		if (result != vk::Result::eSuccess) {
			std::string str = std::format("volkInitialize failed: {}\n", to_string(result));
			std::cerr << str;
			throw std::runtime_error(str);
		}
		VULKAN_HPP_DEFAULT_DISPATCHER.init();
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
		});
}

static std::shared_ptr<VKCore::VulkanDevice> CreateVKDevice(std::shared_ptr<VKCore::VulkanInstance> instance, VKCore::VulkanPhysicalDeviceInfo& info, const std::vector<std::string>& extraDeviceExtensions = {})
{
	auto vulkanDevice = std::make_shared<VKCore::VulkanDevice>();

	// 扩展名不允许重复提交，宿主追加的扩展可能和引擎必需的重叠，这里统一去重。
	std::vector<std::string> requestedExtensions;
	auto addExtension = [&vulkanDevice, &requestedExtensions](const std::string& name) {
		if (std::find(requestedExtensions.begin(), requestedExtensions.end(), name) != requestedExtensions.end())
			return;
		requestedExtensions.push_back(name);
		vulkanDevice->AddDeviceExtension(name);
		};

	addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	addExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	addExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	addExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	addExtension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	addExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
	//vulkanDevice->AddDeviceExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

	if (GlobalConfig::RTCoreEnable)
	{
		for (const auto& name : VKCore::RTCoreExtension)
			addExtension(name);
		if (GlobalConfig::RTCoreReorderEnable)
			for (const auto& name : VKCore::RTCoreReorderExtension)
				addExtension(name);
	}

	if (GlobalConfig::VK_DESCRIPTOR_HEAP_Enable)
		for (const auto& name : VKCore::VK_DESCRIPTOR_HEAP_Extension)
			addExtension(name);

	// 宿主额外声明的设备扩展。
	for (const auto& name : extraDeviceExtensions)
		addExtension(name);

	if (vulkanDevice->Create(instance, info) != vk::Result::eSuccess)
		return nullptr;

	return vulkanDevice;
}

std::shared_ptr<VKCore::VulkanInstance> VKContext::CreateInstance(const std::vector<std::string>& extensionNames)
{
	NeedVulkanBaseInitlized();
	auto instance = std::make_shared<VKCore::VulkanInstance>();
	for (auto& name : extensionNames)
		instance->AddInstanceExtension(name);

	instance->SetLatestApiVersion();
	if (instance->CreateInstance() != vk::Result::eSuccess)
		return nullptr;

	SetInstance(instance);
	return instance;
}

std::shared_ptr<VKCore::VulkanDevice> VKContext::CreateDevice(const std::vector<std::string>& extraDeviceExtensions)
{
	auto instance = _instance;
	if (!instance)
	{
		std::cout << std::format("[ VKContext::CreateDevice ] ERROR\nNo Vulkan instance, call CreateInstance first\n");
		return nullptr;
	}

	// 引擎只做离屏渲染，不需要呈现队列
	VKCore::VulkanPhysicalDeviceInfo info;
	if (!instance->GetSuitablePhysicalDevice(info, true, true))
	{
		std::cout << std::format("[ VKContext::CreateDevice ] ERROR\nFailed to get suitable physical device\n");
		return nullptr;
	}

	auto device = CreateVKDevice(instance, info, extraDeviceExtensions);
	if (!device)
		return nullptr;

	SetDevice(device);
	return device;
}

VKContext* VKContext::Instance()
{
	static VKContext* m_instance = new VKContext();
	return m_instance;
}

void VKContext::SetInstance(std::shared_ptr<VKCore::VulkanInstance> instance)
{
	_instance = instance;
}

void VKContext::SetDevice(std::shared_ptr<VKCore::VulkanDevice> device)
{
	_device = device;
	//_commandPool.reset();
}

std::shared_ptr<VKCore::VulkanInstance> VKContext::GetInstance()
{
	return _instance;
}

std::shared_ptr<VKCore::VulkanDevice> VKContext::GetDevice()
{
	return _device;
}

VkInstance VKContext::GetInstanceHandle()
{
	return _instance->GetHandle();
}

vk::Device VKContext::GetDeviceHandle()
{
	return _device->GetHandle();
}

VmaAllocator VKContext::GetVmaAllocator()
{
	return _device->GetAllocator();
}

vk::DescriptorPool VKContext::GetDescriptorPool()
{
	NeedDescriptorPool();
	return _descriptorPool;
}

void VKContext::SubmitCommandBufferToPendingQueue(std::shared_ptr<VKWrapper::VKCommandBuffer> buffer, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	if (buffer->IsRecording())
		buffer->End();

	SubmitCMDData data;
	data.buffers = { buffer };
	data.syncSeamphore = syncSeamphore;
	data.signalFence = signalFence;
	LockGuard guard(_pendingCommandBufferMutex);
	_pendingCommandBuffers.push_back(std::move(data));
}

void VKContext::SubmitCommandBufferToPendingQueue(std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> buffers, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	for (auto& cmd : buffers)
	{
		if (cmd->IsRecording())
			cmd->End();
	}

	SubmitCMDData data;
	data.buffers = buffers;
	data.syncSeamphore = syncSeamphore;
	data.signalFence = signalFence;
	LockGuard guard(_pendingCommandBufferMutex);
	_pendingCommandBuffers.push_back(std::move(data));
}

vk::Result VKContext::SubmitCommandImmediately(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	if (buffer->IsRecording())
		buffer->End();

	return SubmitCommandBuffer(_device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), { buffer }, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores, signalFence);
}

vk::Result VKContext::SubmitCommandImmediately(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffers, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	for (auto& cmd : buffers)
	{
		if (cmd->IsRecording())
			cmd->End();
	}

	return SubmitCommandBuffer(_device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), buffers, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores, signalFence);
}

vk::Result VKContext::SubmitCommandImmediatelyAndWait(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore)
{
	if (buffer->IsRecording())
		buffer->End();

	auto fence = std::make_shared<VKWrapper::VKFence>(_device.get());
	if (auto result = SubmitCommandBuffer(_device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), { buffer }, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores, fence); result != vk::Result::eSuccess)
		return result;
	if (auto result = fence->Wait(); result != vk::Result::eSuccess)
		return result;
	return vk::Result::eSuccess;
}

vk::Result VKContext::SubmitCommandImmediatelyAndWait(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffers, const CmdSyncSeamphore& syncSeamphore)
{
	for (auto& cmd : buffers)
	{
		if (cmd->IsRecording())
			cmd->End();
	}

	auto fence = std::make_shared<VKWrapper::VKFence>(_device.get());
	if (auto result = SubmitCommandBuffer(_device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), buffers, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores, fence); result != vk::Result::eSuccess)
		return result;
	if (auto result = fence->Wait(); result != vk::Result::eSuccess)
		return result;
	return vk::Result::eSuccess;
}

void VKContext::ProcessPendingCommandAndWait()
{
	auto device = _device;
	if (!device)
		return;

	auto graphicsQueue = device->GetGraphicsQueue();

	if (_pendingCommandBuffers.empty())
		return;

	std::vector<SubmitCMDData> temp;

	{
		LockGuard guard(_pendingCommandBufferMutex);
		_pendingCommandBuffers.swap(temp);
	}

	if (temp.empty())
		return;

	std::vector<WaitSemaphoreData> allWaits;
	allWaits.reserve(temp.size());

	for (int i = 0; i < temp.size(); i++)
	{

		auto& submitData = temp[i];
		auto& waitSemaphores = submitData.syncSeamphore.waitSemaphores;
		auto& signalSemaphores = submitData.syncSeamphore.signalSemaphores;

		auto sem = std::make_shared<VKWrapper::VKBinarySemaphore>(device.get());
		WaitSemaphoreData wait{ .semaphore = sem };
		SignalSemaphoreData signal{ .semaphore = sem };
		signalSemaphores.push_back(std::move(signal));

		vk::Result result = SubmitCommandBuffer(device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), submitData.buffers, submitData.syncSeamphore.waitSemaphores, signalSemaphores, submitData.signalFence);
		if (result != vk::Result::eSuccess)
			outStream << std::format("[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));

		allWaits.push_back(std::move(wait));
	}

	auto fence = std::make_shared<VKWrapper::VKFence>(_device.get());

	vk::Result result = SubmitCommandBuffer(device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), {}, allWaits, {}, fence);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));

	if (auto result = fence->Wait(); result != vk::Result::eSuccess)
		outStream << std::format("[ ProcessPendingCommandAndWait ] ERROR\nFailed to wait fence!\nError code: {}\n", to_string(result));

}

VKContext::VKContext()
{}
VKContext::~VKContext()
{}

void VKContext::NeedDescriptorPool()
{
	if (_descriptorPool)
		return;

	LockGuard guard(_descriptorPoolCreateMutex);

	if (_descriptorPool)
		return;

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		// 1. Uniform Buffer (UBO)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eUniformBuffer)
			.setDescriptorCount(2000),

		// 2. Dynamic Uniform Buffer
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eUniformBufferDynamic)
			.setDescriptorCount(2000),

		// 3. Storage Buffer (SSBO)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eStorageBuffer)
			.setDescriptorCount(2000),

		// Dynamic Storage Buffer
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eStorageBufferDynamic)
			.setDescriptorCount(2000),

		// Combined Image Sampler (纹理 + 采样器)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eCombinedImageSampler)
			.setDescriptorCount(20000),

		// Sampled Image (只读纹理，需要单独绑定采样器)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eSampledImage)
			.setDescriptorCount(4000),

		// Sampler (采样器)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eSampler)
			.setDescriptorCount(4000),

		// Storage Image (可读写纹理，常用于计算着色器)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eStorageImage)
			.setDescriptorCount(2000),

		// Uniform Texel Buffer (以纹理形式访问的缓冲区)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eUniformTexelBuffer)
			.setDescriptorCount(4000),

		// Storage Texel Buffer (可读写纹理缓冲区)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eStorageTexelBuffer)
			.setDescriptorCount(2000),

		// Input Attachment (用于 RenderPass 内的输入附件)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eInputAttachment)
			.setDescriptorCount(500),

		// Inline Uniform Block (内联 UBO，较少用)
		//vk::DescriptorPoolSize()
		//	.setType(vk::DescriptorType::eInlineUniformBlock)
		//	.setDescriptorCount(100),

		// Acceleration Structure (用于光线追踪)
		vk::DescriptorPoolSize()
			.setType(vk::DescriptorType::eAccelerationStructureKHR)
			.setDescriptorCount(500),
	};

	vk::DescriptorPoolCreateInfo poolInfo;
	poolInfo
		.setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
		.setMaxSets(20000)
		.setPoolSizes(poolSizes);
	auto [poolResult, descriptorPool] = _device->GetHandle().createDescriptorPool(poolInfo);
	if (poolResult == vk::Result::eSuccess)
		_descriptorPool = descriptorPool;
}

SpinLock& VKContext::GetDescriptorPoolRequestMutex() {
	return _descriptorPoolRequestMutex;
}