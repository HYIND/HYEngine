#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/VKContext.h"

DynamicBlock::BufferUsageInfo DynamicBlock::GetBufferUsageInfo(BufferUsage usage)
{
	using PS = vk::PipelineStageFlagBits2;
	using AF = vk::AccessFlagBits2;

	BufferUsageInfo info{ PS::eNone, AF::eNone };

	auto add = [&](BufferUsage flag, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access) {
		if (Any(usage, flag)) {
			info.stage |= stage;
			info.access |= access;
		}
		};

	add(BufferUsage::TransferWrite,					PS::eTransfer, AF::eTransferWrite);
	add(BufferUsage::TransferRead,					PS::eTransfer, AF::eTransferRead);
	add(BufferUsage::StorageWrite,					PS::eVertexShader | PS::eFragmentShader | PS::eComputeShader | PS::eRayTracingShaderKHR, AF::eShaderWrite);
	add(BufferUsage::StorageRead,					PS::eVertexShader | PS::eFragmentShader | PS::eComputeShader | PS::eRayTracingShaderKHR, AF::eShaderRead);
	add(BufferUsage::UniformRead,					PS::eVertexShader | PS::eFragmentShader | PS::eComputeShader | PS::eRayTracingShaderKHR, AF::eUniformRead);
	add(BufferUsage::VertexAttributeRead,			PS::eVertexInput, AF::eVertexAttributeRead);
	add(BufferUsage::IndexRead,						PS::eVertexInput, AF::eIndexRead);
	add(BufferUsage::IndirectRead,					PS::eDrawIndirect, AF::eIndirectCommandRead);
	add(BufferUsage::AccelerationStructureRead,		PS::eRayTracingShaderKHR, AF::eAccelerationStructureReadKHR);
	add(BufferUsage::AccelerationStructureWrite,	PS::eAccelerationStructureBuildKHR, AF::eAccelerationStructureWriteKHR);
	add(BufferUsage::ShaderBindingTableRead,		PS::eRayTracingShaderKHR, AF::eShaderBindingTableReadKHR);

	return info;
}

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

void DynamicBlock::SetSizeAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, uint64_t newsize)
{
	LockGuard guard(_mutex);

	bool update = newsize > _size || !_curBuffer;
	if (!update)
		return;

	auto newBuffer = std::make_shared<VKWrapper::VmaBuffer>(_device, newsize, _usage);
	if (_size > 0 && _curBuffer)
		VKWrapper::VmaBuffer::CopyBufferAsync(cmd, *_curBuffer, *newBuffer, _size);

	_curBuffer = newBuffer;
	_size = newsize;
}

void DynamicBlock::WriteDataAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const void* data, uint64_t size, uint64_t offset)
{
	LockGuard guard(_mutex);
	if (size + offset > _size)
		SetSizeAsync(cmd, size + offset);

	if (_curBuffer)
		_curBuffer->UpdateAsync(cmd, data, size, offset);
}

void DynamicBlock::CopySelfDataAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	LockGuard guard(_mutex);
	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	VKWrapper::VmaBuffer::CopyBufferAsync(cmd, *_curBuffer, *_curBuffer, _size, srcFirst, destFirst);
}

void DynamicBlock::Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, BufferUsage pre, BufferUsage cur, vk::DependencyFlags flags)
{
	auto preInfo = GetBufferUsageInfo(pre);
	auto curInfo = GetBufferUsageInfo(cur);

	vk::BufferMemoryBarrier2 barrier;
	barrier
		.setSrcStageMask(preInfo.stage)
		.setSrcAccessMask(preInfo.access)
		.setDstStageMask(curInfo.stage)
		.setDstAccessMask(curInfo.access)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setBuffer(_curBuffer->GetHandle())
		.setOffset(0)
		.setSize(vk::WholeSize);

	cmd->pipelineBarrier2(barrier, flags);
}

std::shared_ptr<VKWrapper::VmaBuffer> DynamicBlock::GetBuffer() const { return _curBuffer; }

vk::Buffer DynamicBlock::GetHandle() const { return _curBuffer->GetHandle(); }

vk::DeviceAddress DynamicBlock::GetDeviceAddress() const {
	vk::BufferDeviceAddressInfo addressInfo;
	addressInfo.setBuffer(_curBuffer->GetHandle());
	return _device->GetHandle().getBufferAddress(addressInfo);
}

uint64_t DynamicBlock::GetSize() const { return _size; }

