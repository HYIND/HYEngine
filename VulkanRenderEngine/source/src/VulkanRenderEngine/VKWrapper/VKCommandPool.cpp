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
		_device->GetHandle().destroyCommandPool(_handle);

	_cmdResPool.Clear();
	_handle = VK_NULL_HANDLE;
	_device = nullptr;
	_queueType = QueueFamilyType::Graphics;
	_queueFamilyIndex = 0;
}

vk::CommandPool VKCommandPool::GetHandle() const { return  _handle; }

SpinLock& VKWrapper::VKCommandPool::GetCommandPoolMutex() { return _commandPoolMutex; }

vk::Result VKCommandPool::AllocateBuffers(std::shared_ptr<VKCommandBuffer>& buffer, vk::CommandBufferLevel level)
{
	vk::CommandBuffer handle = _cmdResPool.FetchHandle();
	if (handle == VK_NULL_HANDLE)
	{
		vk::CommandBufferAllocateInfo allocateInfo;
		allocateInfo
			.setCommandPool(_handle)
			.setLevel(level)
			.setCommandBufferCount(1);


		LockGuard guard(_commandPoolMutex);
		auto [res, hs] = _device->GetHandle().allocateCommandBuffers(allocateInfo);
		if (res != vk::Result::eSuccess)
		{
			outStream << std::format("[ VKCommandPool ] ERROR\nFailed to allocate command buffers!\nError code: {}\n", to_string(res));
			return res;
		}

		handle = hs[0];
	}

	buffer = std::make_shared<VKCommandBuffer>();
	buffer->_handle = handle;
	buffer->_pool = this;

	return vk::Result::eSuccess;
}

void VKCommandPool::FreeBuffers(VKCommandBuffer* buffer) {
	if (!buffer)
		return;

	vk::CommandBuffer bufferhandle = buffer->GetHandle();
	if (bufferhandle == VK_NULL_HANDLE)
		return;

	LockGuard guard(_commandPoolMutex);
	if (buffer->IsRecording())
		buffer->Reset();
	if (!_cmdResPool.RecycleHandle((VkCommandBuffer)bufferhandle))
		_device->GetHandle().freeCommandBuffers(_handle, 1, &bufferhandle);
}

void VKCommandPool::Trim(vk::CommandPoolTrimFlags flags) {
	LockGuard guard(_commandPoolMutex);
	_device->GetHandle().trimCommandPool(_handle, flags);
}


VKWrapper::VKCommandPool::CmdResPool::CmdResPool(uint32_t maxResNum) :_maxResNum(maxResNum) {}

VkCommandBuffer VKWrapper::VKCommandPool::CmdResPool::FetchHandle()
{
	if (_iDleList.size() > 0) // 不为空，从中取一个
	{
		auto it = _iDleList.begin();
		auto handle = *it;
		_iDleList.erase(it);
		return handle;
	}
	return VK_NULL_HANDLE;
}

bool VKWrapper::VKCommandPool::CmdResPool::RecycleHandle(VkCommandBuffer handle)
{
	// 已持有的数据
	auto it = _datas.find(handle);
	if (it != _datas.end())
	{
		if (_iDleList.find(handle) == _iDleList.end())
		{
			_iDleList.insert(handle);
			return true;
		}
		else
		{
			std::cerr << "CmdResPool::RecycleData double free!!!\n";
			return true;
		}
	}
	else // 外部数据，非池子持有的
	{
		if (_datas.size() < _maxResNum)
		{
			_datas.insert(handle);
			_iDleList.insert(handle);
			return true;
		}
		else
		{
			return false;
		}
	}
}

void VKWrapper::VKCommandPool::CmdResPool::Clear() {
	_iDleList.clear();
	_datas.clear();
}
