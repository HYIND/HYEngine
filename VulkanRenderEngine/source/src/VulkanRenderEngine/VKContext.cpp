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
	// 设备 / 描述符池已经销毁：描述符集随池子一起没了
	if (!_device || !_descriptorPool)
		return vk::Result::eSuccess;
	return _device->GetHandle().freeDescriptorSets(_descriptorPool, sets);
}

void VKContext::Retire(VKWrapper::IVKResource* res) {
	if (!res) return;
	// 设备已经销毁：句柄都跟着设备没了，不能再调 Destroy（会用到已销毁的设备 / VMA 分配器）
	if (_retireClosed.load(std::memory_order_acquire))
	{
		delete res;
		return;
	}
	LockGuard guard(_pendingDestoryResourceMutex);
	_pendingDestoryResource.push_back(PendingResource{ res, _frameIndex.load() });
}

void VKContext::ProcessRetireAndPushFrameIndex() {
	if (_retireClosed.load(std::memory_order_acquire) || _pendingDestoryResource.empty())
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

void VKContext::FlushRetiredResources() {
	if (_device)
		_device->WaitIdle();

	// 有些 Destroy 会再 Retire（释放材质 → 段缓冲，回收 bindless 槽位 → 旧的 image），所以要循环到队列为空
	constexpr uint32_t kMaxRounds = 32;
	for (uint32_t round = 0; round < kMaxRounds; ++round)
	{
		std::list<PendingResource> temp;
		{
			LockGuard guard(_pendingDestoryResourceMutex);
			if (_pendingDestoryResource.empty())
				return;
			temp.swap(_pendingDestoryResource);
		}

		for (auto& p : temp) {
			if (p.res)
			{
				p.res->Destroy();
				delete p.res;
			}
		}
	}

	LockGuard guard(_pendingDestoryResourceMutex);
	std::cerr << std::format("[ VKContext ] WARNING: 延迟销毁队列执行了 {} 轮还没清空（剩 {} 个），可能有 Destroy 在反复 Retire\n",
		kMaxRounds, _pendingDestoryResource.size());
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

	// 设备还没建 / 已经销毁
	auto device = VKCONTEXT->GetDevice();
	if (!device)
		return;

	auto commandPool = std::make_shared<VKWrapper::VKCommandPool>(device.get(), VKWrapper::VKCommandPool::QueueFamilyType::Graphics, vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	if (!commandPool || !commandPool->GetHandle())
		return;
	_commandPool = std::move(commandPool);
	VKCONTEXT->RegisterThreadContext(this);
}

VKThreadContext::~VKThreadContext()
{
	// 先从登记表里摘掉（和 Shutdown 互斥），再随成员一起释放命令池。
	// Shutdown 已经释放过的话 _commandPool 是空的，这里什么都不用做
	VKContext::Instance()->UnregisterThreadContext(this);
}

void VKContext::RegisterThreadContext(VKThreadContext* context)
{
	LockGuard guard(_threadContextsMutex);
	_threadContexts.insert(context);
}

void VKContext::UnregisterThreadContext(VKThreadContext* context)
{
	LockGuard guard(_threadContextsMutex);
	_threadContexts.erase(context);
}

void VKContext::AddShutdownCallback(std::function<void()> callback)
{
	if (!callback)
		return;
	LockGuard guard(_shutdownCallbacksMutex);
	_shutdownCallbacks.push_back(std::move(callback));
}

void VKContext::Shutdown()
{
	if (_retireClosed.load(std::memory_order_acquire))
		return;

	auto device = _device;
	if (device)
	{
		// 1. 引擎、宿主都已经停了：等 GPU 做完，执行完它们延迟销毁的资源
		FlushRetiredResources();

		// 2. 全局缓存（bindless 表、间接绘制的段缓冲……）：倒序释放，释放出来的又进延迟销毁队列
		std::vector<std::function<void()>> callbacks;
		{
			LockGuard guard(_shutdownCallbacksMutex);
			callbacks.swap(_shutdownCallbacks);
		}
		for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it)
			(*it)();

		// 3. 还没提交的命令缓冲、池子里的 fence：放掉（它们的释放也走延迟销毁，要在命令池之前）
		{
			LockGuard guard(_pendingCommandBufferMutex);
			_pendingCommandBuffers.clear();
			_hasPendingCommands.store(false, std::memory_order_relaxed);
		}
		{
			LockGuard guard(_waitFencePoolMutex);
			_waitFencePool.clear();
		}
		FlushRetiredResources();

		// 4. 各线程的命令池：主线程的 thread_local 要到 main 返回后才析构，那时设备早没了
		{
			LockGuard guard(_threadContextsMutex);
			for (auto* context : _threadContexts)
				context->ReleaseCommandPool();
			_threadContexts.clear();
		}

		// 5. 描述符池（ImGui 后端也从这里分配，宿主要先关掉 ImGui 后端）
		{
			LockGuard guard(_descriptorPoolRequestMutex);
			if (_descriptorPool)
			{
				device->GetHandle().destroyDescriptorPool(_descriptorPool);
				_descriptorPool = VK_NULL_HANDLE;
			}
		}
	}

	// 6. 关掉延迟销毁：之后静态对象析构时释放的资源直接丢弃
	_retireClosed.store(true, std::memory_order_release);
	{
		LockGuard guard(_pendingDestoryResourceMutex);
		if (!_pendingDestoryResource.empty())
			std::cerr << std::format("[ VKContext ] WARNING: 销毁设备时延迟销毁队列里还剩 {} 个资源，已丢弃\n", _pendingDestoryResource.size());
		for (auto& p : _pendingDestoryResource)
			delete p.res;
		_pendingDestoryResource.clear();
	}

	// 7. 设备、实例
	_device.reset();
	if (device)
		device->Release();
	device.reset();

	if (auto instance = std::move(_instance))
		instance->Release();
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
	_hasPendingCommands.store(true, std::memory_order_release);
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
	_hasPendingCommands.store(true, std::memory_order_release);
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

	return SubmitAndWaitWithPooledFence({ buffer }, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores);
}

vk::Result VKContext::SubmitCommandImmediatelyAndWait(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffers, const CmdSyncSeamphore& syncSeamphore)
{
	for (auto& cmd : buffers)
	{
		if (cmd->IsRecording())
			cmd->End();
	}

	return SubmitAndWaitWithPooledFence(buffers, syncSeamphore.waitSemaphores, syncSeamphore.signalSemaphores);
}

std::shared_ptr<VKWrapper::VKFence> VKContext::AcquireWaitFence()
{
	{
		LockGuard guard(_waitFencePoolMutex);
		if (!_waitFencePool.empty())
		{
			auto fence = std::move(_waitFencePool.back());
			_waitFencePool.pop_back();
			return fence;
		}
	}
	auto fence = std::make_shared<VKWrapper::VKFence>(_device.get());
	if (!fence->GetHandle())
		return nullptr;
	return fence;
}

void VKContext::RecycleWaitFence(std::shared_ptr<VKWrapper::VKFence> fence)
{
	// 池子上限：同时在等的线程数有限，多出来的照常释放（走延迟销毁）
	constexpr size_t kMaxPooledFences = 64;
	if (!fence || !fence->GetHandle() || _retireClosed.load(std::memory_order_acquire))
		return;
	LockGuard guard(_waitFencePoolMutex);
	if (_waitFencePool.size() < kMaxPooledFences)
		_waitFencePool.push_back(std::move(fence));
}

vk::Result VKContext::SubmitAndWaitWithPooledFence(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffers,
	const std::vector<WaitSemaphoreData>& waitSemaphores, const std::vector<SignalSemaphoreData>& signalSemaphores)
{
	auto fence = AcquireWaitFence();
	if (!fence)
		return vk::Result::eErrorOutOfDeviceMemory;

	if (auto result = SubmitCommandBuffer(_device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), buffers, waitSemaphores, signalSemaphores, fence); result != vk::Result::eSuccess)
	{
		// 提交失败：fence 没交给队列，还是未置位，可以直接放回
		RecycleWaitFence(std::move(fence));
		return result;
	}

	if (auto result = fence->Wait(); result != vk::Result::eSuccess)
		return result;	// 等失败（设备丢失等）：fence 状态不明，不回收

	// 置位了：重置后放回。重置失败就不回收
	if (fence->Reset() == vk::Result::eSuccess)
		RecycleWaitFence(std::move(fence));
	return vk::Result::eSuccess;
}

void VKContext::ProcessPendingCommand()
{
	// 快速路径：不拿锁。标记只在锁里改，这里读到旧值最多晚一帧提交（或多拿一次锁），不会漏
	if (!_hasPendingCommands.load(std::memory_order_acquire))
		return;

	auto device = _device;
	if (!device)
		return;

	std::vector<SubmitCMDData> temp;
	{
		LockGuard guard(_pendingCommandBufferMutex);
		_pendingCommandBuffers.swap(temp);
		_hasPendingCommands.store(false, std::memory_order_relaxed);
	}

	// 按加入顺序逐批提交，各批自己的信号量 / fence 照常带上。只提交、不等：
	// 命令缓冲、信号量释放时都走延迟销毁，调用方要等结果就自己带 fence 或信号量
	for (auto& submitData : temp)
	{
		vk::Result result = SubmitCommandBuffer(device->GetGraphicsQueue(), device->GetGraphicsQueueMutex(), submitData.buffers,
			submitData.syncSeamphore.waitSemaphores, submitData.syncSeamphore.signalSemaphores, submitData.signalFence);
		if (result != vk::Result::eSuccess)
			outStream << std::format("[ ProcessPendingCommand ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));
	}
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

	if (_descriptorPool || !_device)
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