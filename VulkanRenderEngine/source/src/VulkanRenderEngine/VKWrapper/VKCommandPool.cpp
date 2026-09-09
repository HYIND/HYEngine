#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandPool.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandBuffer.h"
#include "CriticalSectionLock.h"

using namespace VKWrapper;


VKCommandPool::VKCommandPool(VKCore::VulkanDevice* device, QueueFamilyType type, vk::CommandPoolCreateFlags flags)
{
	Create(device, type, flags);
}

VKCommandPool::~VKCommandPool() { Release(); }

vk::Result VKCommandPool::Create(VKCore::VulkanDevice* device, QueueFamilyType type, vk::CommandPoolCreateFlags flags)
{
	Release();

	switch (type)
	{
	case VKCommandPool::QueueFamilyType::Graphics:
		_queueFamilyIndex = device->GetGraphicsQueueFamily();
		break;
	case VKCommandPool::QueueFamilyType::Present:
		_queueFamilyIndex = device->GetPresentQueueFamily();
		break;
	case VKCommandPool::QueueFamilyType::Compute:
		_queueFamilyIndex = device->GetComputeQueueFamily();
		break;
	default:
		break;
	}

	vk::CommandPoolCreateInfo createInfo;
	createInfo
		.setFlags(flags)
		.setQueueFamilyIndex(_queueFamilyIndex);

	auto [result, handle] = device->GetHandle().createCommandPool(createInfo);
	if (result != vk::Result::eSuccess)
	{
		outStream << std::format("[ VKCommandPool ] ERROR\nFailed to create a command pool!\nError code: {}\n", to_string(result));
	}
	else
	{
		_handle = handle;
		_device = device;
		_queueType = type;
		_queueFamilyIndex = createInfo.queueFamilyIndex;
	}

	return result;
}

void VKCommandPool::Release() {
	if (_handle && _device)
		vkDestroyCommandPool(_device->GetHandle(), _handle, nullptr);

	_handle = VK_NULL_HANDLE;
	_device = nullptr;
	_queueType = QueueFamilyType::Graphics;
	_queueFamilyIndex = 0;
}

vk::CommandPool VKCommandPool::GetHandle() const { return  _handle; }

//SpinLock& VKWrapper::VKCommandPool::GetCommandPoolMutex() { return _commandPoolMutex; }

vk::Result VKCommandPool::AllocateBuffers(VKCommandBuffer* buffer, vk::CommandBufferLevel level) {
	if (!buffer)
		return vk::Result::eErrorInitializationFailed;

	vk::CommandBufferAllocateInfo allocateInfo;
	allocateInfo
		.setCommandPool(_handle)
		.setLevel(level)
		.setCommandBufferCount(1);

	vk::CommandBuffer handle;
	{
		//LockGuard guard(_commandPoolMutex);
		auto [res, hs] = _device->GetHandle().allocateCommandBuffers(allocateInfo);
		if (res != vk::Result::eSuccess)
		{
			outStream << std::format("[ VKCommandPool ] ERROR\nFailed to allocate command buffers!\nError code: {}\n", to_string(res));
			return res;
		}

		handle = hs[0];
	}

	buffer->Release();
	buffer->_handle = handle;
	buffer->_pool = this;

	return vk::Result::eSuccess;
}

vk::Result VKCommandPool::AllocateBuffers(std::shared_ptr<VKCommandBuffer> buffer, vk::CommandBufferLevel level)
{
	return AllocateBuffers(buffer.get(), level);
}

vk::Result VKCommandPool::AllocateBuffers(std::vector<std::shared_ptr<VKCommandBuffer>>& buffers, vk::CommandBufferLevel level) {
	if (buffers.empty())
		return vk::Result::eErrorInitializationFailed;

	vk::CommandBufferAllocateInfo allocateInfo;
	allocateInfo
		.setCommandPool(_handle)
		.setLevel(level)
		.setCommandBufferCount(buffers.size());

	std::vector<vk::CommandBuffer> handles;

	{
		//LockGuard guard(_commandPoolMutex);
		auto [res, hs] = _device->GetHandle().allocateCommandBuffers(allocateInfo);
		if (res != vk::Result::eSuccess)
		{
			outStream << std::format("[ VKCommandPool ] ERROR\nFailed to allocate command buffers!\nError code: {}\n", to_string(res));
			return res;
		}

		handles = std::move(hs);
	}

	for (int i = 0; i < handles.size(); i++)
	{
		buffers[i]->Release();
		buffers[i]->_handle = handles[i];
		buffers[i]->_pool = this;
	}

	return vk::Result::eSuccess;
}

void VKCommandPool::FreeBuffers(VKCommandBuffer* buffer) {
	if (!buffer)
		return;
	vk::CommandBuffer bufferhandle = buffer->GetHandle();
	//LockGuard guard(_commandPoolMutex);
	_device->GetHandle().freeCommandBuffers(_handle, 1, &bufferhandle);
}

void VKCommandPool::FreeBuffers(std::shared_ptr<VKCommandBuffer> buffer) {
	if (!buffer)
		return;
	vk::CommandBuffer bufferhandle = buffer->GetHandle();
	//LockGuard guard(_commandPoolMutex);
	_device->GetHandle().freeCommandBuffers(_handle, 1, &bufferhandle);
}

void VKCommandPool::FreeBuffers(std::vector<std::shared_ptr<VKCommandBuffer>>& buffers) {
	if (buffers.empty())
		return;
	std::vector<vk::CommandBuffer> bufferhandles(buffers.size(), VK_NULL_HANDLE);
	for (int i = 0; i < buffers.size(); i++)
		bufferhandles[i] = buffers[i]->_handle;
	//LockGuard guard(_commandPoolMutex);
	_device->GetHandle().freeCommandBuffers(_handle, bufferhandles);
}

void VKCommandPool::Trim(vk::CommandPoolTrimFlags flags) {
	//LockGuard guard(_commandPoolMutex);
	_device->GetHandle().trimCommandPool(_handle, flags);
}
