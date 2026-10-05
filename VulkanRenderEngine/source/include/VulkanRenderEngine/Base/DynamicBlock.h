#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"
#include "VulkanRenderEngine/VKContext.h"

class VKWrapper::VKCommandBuffer;

// 动态数据块，处理vk::buffer不可变的局限性
// 通过可替换的std::shared_ptr<VKWrapper::VmaBuffer>实现扩容
// 管理的VmaBuffer是可变的，如果扩容会重新创建新的VmaBuffer
// 实际使用可实时GetBuffer获取最新的VmaBuffer，由shared_ptr机制保证VmaBuffer生命周期

enum class BufferUsage :uint32_t {
	None = 0,
	TransferWrite = 1 << 0,
	TransferRead = 1 << 1,
	StorageWrite = 1 << 2,
	StorageRead = 1 << 3,
	UniformRead = 1 << 4,
	VertexAttributeRead = 1 << 5,
	IndexRead = 1 << 6,
	IndirectRead = 1 << 7,
	AccelerationStructureWrite = 1 << 8,	// 构建加速结构时写
	AccelerationStructureRead = 1 << 9,		// 光追着色器读 TLAS/BLAS
	ShaderBindingTableRead = 1 << 10
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
	return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
	return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) {
	a = a | b; return a;
}
inline bool Any(BufferUsage a, BufferUsage b) {
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

class DynamicBlock
{

public:
	struct BufferUsageInfo {
		vk::PipelineStageFlags2 stage;
		vk::AccessFlags2 access;
	};

	static BufferUsageInfo GetBufferUsageInfo(BufferUsage usage);

public:
	static bool MemCopyAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, DynamicBlock& src, DynamicBlock& dst, uint32_t size, uint32_t srcOffset = 0, uint32_t dstOffset = 0);
	static bool MemCopyAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, std::shared_ptr<DynamicBlock> src, std::shared_ptr<DynamicBlock> dst, uint32_t size, uint32_t srcOffset = 0, uint32_t dstOffset = 0);
	static bool MemCopy(std::shared_ptr<DynamicBlock> src, std::shared_ptr<DynamicBlock> dst, uint32_t size, uint32_t srcOffset = 0, uint32_t dstOffset = 0);

public:
	DynamicBlock(uint64_t size = 0, VKWrapper::VmaBuffer::Usage usage = VKWrapper::VmaBuffer::Usage::None, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get());
	~DynamicBlock();

	void SetSize(uint64_t newsize, bool remainData = true);
	void WriteData(const void* data, uint64_t size, uint64_t offset = 0);
	void CopySelfData(uint64_t destFirst, uint64_t srcFirst, uint64_t length); //buffer内部数据之间拷贝

	void SetSizeAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, uint64_t newsize, bool remainData = true);
	void WriteDataAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const void* data, uint64_t size, uint64_t offset = 0);
	void CopySelfDataAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, uint64_t destFirst, uint64_t srcFirst, uint64_t length); //buffer内部数据之间拷贝

	bool Readback(void* outData, size_t size, size_t offset = 0);
	bool Readback(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset = 0);

	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur,
		vk::DependencyFlags flags = {});

	std::shared_ptr<VKWrapper::VmaBuffer> GetBuffer() const;
	vk::Buffer GetHandle() const;
	vk::DeviceAddress GetDeviceAddress() const;
	uint64_t GetSize() const;

private:
	void Need();

protected:
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
	UniformBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::UniformBuffer | extraUsage, device) {}
	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::UniformRead,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};

class StorageBlock : public DynamicBlock
{
public:
	StorageBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::StorageBuffer | extraUsage, device) {}

	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::StorageRead | BufferUsage::StorageWrite,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};

class VertexBufferBlock : public StorageBlock
{
public:
	VertexBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: StorageBlock(size, device, VKWrapper::VmaBuffer::Usage::VertexBuffer | extraUsage) {}

	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::VertexAttributeRead | BufferUsage::StorageRead | BufferUsage::StorageWrite,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};

class IndexBufferBlock : public StorageBlock
{
public:
	IndexBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: StorageBlock(size, device, VKWrapper::VmaBuffer::Usage::IndexBuffer | extraUsage) {}

	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::IndexRead | BufferUsage::StorageRead | BufferUsage::StorageWrite,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};

class IndirectBufferBlock : public StorageBlock
{
public:
	IndirectBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: StorageBlock(size, device, VKWrapper::VmaBuffer::Usage::IndirectBuffer | extraUsage) {}

	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::IndirectRead | BufferUsage::StorageRead | BufferUsage::StorageWrite,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};

class SBTBufferBlock : public DynamicBlock
{
public:
	SBTBufferBlock(uint64_t size = 0, VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get(), VKWrapper::VmaBuffer::Usage extraUsage = VKWrapper::VmaBuffer::Usage::None)
		: DynamicBlock(size, VKWrapper::VmaBuffer::Usage::SBTBuffer | extraUsage, device) {}
	void Barrier(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		BufferUsage pre,
		BufferUsage cur = BufferUsage::ShaderBindingTableRead,
		vk::DependencyFlags flags = {}) {
		DynamicBlock::Barrier(cmd, pre, cur, flags);
	}
};
