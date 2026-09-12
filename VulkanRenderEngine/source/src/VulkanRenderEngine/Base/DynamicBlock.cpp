#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/VKContext.h"

DynamicBlock::DynamicBlock(uint64_t size, VKWrapper::VmaBuffer::Usage usage, VKCore::VulkanDevice* device)
	:_device(device), _size(0), _usage(usage)
{
	size = std::max((uint64_t)16, size);
	SetSize(size);
}

DynamicBlock::~DynamicBlock()
{
	_curBuffer.reset();
	_size = 0;
}

void DynamicBlock::SetSize(uint64_t newsize)
{
	LockGuard guard(_mutex);

	bool update = newsize > _size || !_curBuffer;
	if (!update)
		return;

	auto newBuffer = std::make_shared<VKWrapper::VmaBuffer>(_device, newsize, _usage);
	if (_size > 0 && _curBuffer)
		VKWrapper::VmaBuffer::CopyBuffer(*_curBuffer, *newBuffer, _size);

	_curBuffer = newBuffer;
	_size = newsize;
}

void DynamicBlock::WriteData(const void* data, uint64_t size, uint64_t offset)
{
	LockGuard guard(_mutex);
	if (size + offset > _size)
		SetSize(size + offset);

	if (_curBuffer)
		_curBuffer->Update(data, size, offset);
}

void DynamicBlock::CopySelfData(uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	LockGuard guard(_mutex);
	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	VKWrapper::VmaBuffer::CopyBuffer(*_curBuffer, *_curBuffer, _size, srcFirst, destFirst);
}

std::shared_ptr<VKWrapper::VmaBuffer> DynamicBlock::GetBuffer() const { return _curBuffer; }

vk::Buffer DynamicBlock::GetHandle() const { return _curBuffer->GetHandle(); }

vk::DeviceAddress DynamicBlock::GetDeviceAddress() const {
	vk::BufferDeviceAddressInfo addressInfo;
	addressInfo.setBuffer(_curBuffer->GetHandle());
	return _device->GetHandle().getBufferAddress(addressInfo);
}

uint64_t DynamicBlock::GetSize() const { return _size; }

