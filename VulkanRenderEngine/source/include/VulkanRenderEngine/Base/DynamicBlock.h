#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"
#include "VulkanRenderEngine/VKContext.h"

class VKWrapper::VKCommandBuffer;

// 动态数据块，处理vk::buffer不可变的局限性
// 通过可替换的std::shared_ptr<VKWrapper::VmaBuffer>实现扩容
// 管理的VmaBuffer是可变的，如果扩容会重新创建新的VmaBuffer
// 实际使用可实时GetBuffer获取最新的VmaBuffer，由shared_ptr机制保证VmaBuffer生命周期
class DynamicBlock
{
public:
	DynamicBlock(uint64_t size = 0, VKWrapper::VmaBuffer::Usage usage = VKWrapper::VmaBuffer::Usage::None, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get());
	~DynamicBlock();

	void SetSize(uint64_t size);
	void WriteData(const void* data, uint64_t size, uint64_t offset = 0);
	void CopySelfData(uint64_t destFirst, uint64_t srcFirst, uint64_t length); //buffer内部数据之间拷贝

	std::shared_ptr<VKWrapper::VmaBuffer> GetBuffer() const;
	uint64_t GetSize();

private:
	void Need();

private:
	VKCore::VulkanDevice* _device;
	std::shared_ptr<VKWrapper::VmaBuffer> _curBuffer;
	uint64_t _size;
	CriticalSectionLock _mutex;
	VKWrapper::VmaBuffer::Usage _usage;
};

//SSBO管理
class UniformBlock : public DynamicBlock
{
public:
	UniformBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get())
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::UniformBuffer, device) {
	}
};

class StorageBlock : public DynamicBlock
{
public:
	StorageBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get())
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::StorageBuffer, device) {
	}
};

class VertexBufferBlock : public DynamicBlock
{
public:
	VertexBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get())
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::VertexBuffer, device) {
	}
};

class IndexBufferBlock : public DynamicBlock
{
public:
	IndexBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get())
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::IndexBuffer, device) {
	}
};

class IndirectBufferBlock : public DynamicBlock
{
public:
	IndirectBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get())
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::IndirectBuffer, device) {
	}
};