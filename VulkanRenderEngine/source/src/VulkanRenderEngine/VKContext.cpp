#include "vkstdafx.h"
#include "VulkanRenderEngine/VKContext.h"
#include <format>
#include <thread>
#include "CriticalSectionLock.h"

thread_local VKThreadContext tls_context;

static vk::Result SubmitCommandBuffer(
	vk::Queue queue,
	SpinLock& queuemutex,
	const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& commandBuffers,
	const std::vector<WaitSemaphoreData>& waitSemaphores = {},
	const std::vector<std::shared_ptr<VKWrapper::VKSemaphore>>& signalSemaphores = {},
	const std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr
)
{

	std::vector<vk::CommandBuffer> cmdHandles;
	std::vector<vk::Semaphore> waitHandles;
	std::vector<vk::PipelineStageFlags> flags;
	std::vector<vk::Semaphore> signalHandles;
	auto fenceHandle = signalFence ? signalFence->GetHandle() : VK_NULL_HANDLE;

	for (auto& cmd : commandBuffers)
		cmdHandles.push_back(cmd->GetHandle());

	if (!waitSemaphores.empty())
	{
		waitHandles.resize(waitSemaphores.size(), VK_NULL_HANDLE);
		flags.resize(waitSemaphores.size(), vk::PipelineStageFlagBits::eColorAttachmentOutput);
		for (int i = 0; i < waitSemaphores.size(); i++)
		{
			auto& data = waitSemaphores[i];
			auto& semaphore = data.semaphore;
			if (semaphore) waitHandles[i] = semaphore->GetHandle();
			flags[i] = vk::PipelineStageFlags(data.flags);
		}
	}

	if (!signalSemaphores.empty())
	{
		signalHandles.resize(signalSemaphores.size(), VK_NULL_HANDLE);
		for (int i = 0; i < signalSemaphores.size(); i++)
		{
			auto& semaphore = signalSemaphores[i];
			if (semaphore) signalHandles[i] = semaphore->GetHandle();
		}
	}

	vk::SubmitInfo submitInfo;
	submitInfo.setWaitSemaphores(waitHandles)
		.setWaitDstStageMask(flags)
		.setCommandBuffers(cmdHandles)
		.setSignalSemaphores(signalHandles);

	LockGuard guard(queuemutex);
	auto result = queue.submit(submitInfo, fenceHandle);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));
	return result;
}


std::shared_ptr<VKWrapper::VKCommandBuffer> VKContext::GetCommandBuffer()
{
	return tls_context.GetCommandBuffer();
}

void VKContext::Retire(VKWrapper::IVKResource* res) {
	LockGuard guard(_pendingDestoryResourceMutex);
	_pendingDestoryResource.push(res);
}

void VKContext::ProcessRetire() {
	if (_pendingDestoryResource.empty())
		return;

	std::queue<VKWrapper::IVKResource*> temp;

	{
		LockGuard guard(_pendingDestoryResourceMutex);
		temp.swap(_pendingDestoryResource);
	}

	while (!temp.empty())
	{
		auto res = temp.front();
		if (res)
		{
			res->Destroy();
			delete res;
		}
		temp.pop();
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

		WaitSemaphoreData data{ .semaphore = std::make_shared<VKWrapper::VKSemaphore>(device.get()) };
		signalSemaphores.push_back(data.semaphore);

		vk::Result result = SubmitCommandBuffer(device->GetGraphicsQueue(), _device->GetGraphicsQueueMutex(), submitData.buffers, submitData.syncSeamphore.waitSemaphores, signalSemaphores, submitData.signalFence);
		if (result != vk::Result::eSuccess)
			outStream << std::format("[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", to_string(result));

		allWaits.push_back(std::move(data));
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

