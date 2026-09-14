#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/GlobalConfig.h"

using namespace VKWrapper;

class RestireBuffer :public IVKResource
{
public:
	RestireBuffer(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation allocation)
		:m_allocator(allocator), m_buffer(buffer), m_allocation(allocation)
	{}
	virtual void Destroy() {
		vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
	}

public:
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	vk::Buffer m_buffer = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
};

VKWrapper::VmaBuffer::VmaBuffer(VKCore::VulkanDevice* vulkanDevice, uint64_t size, Usage usage, bool cpuAccess) {
	Create(vulkanDevice, size, usage, cpuAccess);
}

VmaBuffer::VmaBuffer(VKCore::VulkanDevice* vulkanDevice, const vk::BufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& allocInfo) {
	Create(vulkanDevice, bufferInfo, allocInfo);
}

VmaBuffer::~VmaBuffer() {
	Destroy();
}


// 移动语义
VKWrapper::VmaBuffer::VmaBuffer(VmaBuffer&& other) noexcept { *this = std::move(other); }

VmaBuffer& VmaBuffer::operator=(VmaBuffer&& other) noexcept {
	if (this != &other) {
		Destroy();
		m_allocator = other.m_allocator;
		m_buffer = other.m_buffer;
		m_allocation = other.m_allocation;
		m_size = other.m_size;
		other.m_allocator = VK_NULL_HANDLE;
		other.m_buffer = VK_NULL_HANDLE;
		other.m_allocation = VK_NULL_HANDLE;
	}
	return *this;
}

bool VmaBuffer::Create(VKCore::VulkanDevice* vulkanDevice, const vk::BufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& allocInfo) {
	Destroy();
	m_device = vulkanDevice;
	m_allocator = vulkanDevice->GetAllocator();
	m_size = bufferInfo.size;
	m_bufferInfo = bufferInfo;
	m_allocInfo = allocInfo;
	bool result = vmaCreateBuffer(m_allocator, (VkBufferCreateInfo*)&bufferInfo, &allocInfo, (VkBuffer*)&m_buffer, &m_allocation, nullptr) == VK_SUCCESS;
	if (!result)
		std::cout << std::format("[ VmaBuffer ] CreateBuffer failed!\n");
	return result;
}

bool VmaBuffer::Create(VKCore::VulkanDevice* vulkanDevice, uint64_t size, Usage usage, bool cpuAccess)
{
	vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	if (usage & Usage::UniformBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
	if (usage & Usage::StorageBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
	if (usage & Usage::VertexBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
	if (usage & Usage::IndexBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
	if (usage & Usage::IndirectBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
	if (usage & Usage::SBTBuffer)
		usageFlags |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;

	if (GlobalConfig::RTCoreEnable)
		usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
	usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.
		setSize(size)
		.setUsage(usageFlags)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setFlags({});

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	if (cpuAccess)
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	return Create(vulkanDevice, bufferInfo, allocInfo);
}

void VmaBuffer::Destroy() {
	if (m_allocator && m_buffer && m_allocation) {
		VKCONTEXT->Retire(new RestireBuffer(m_allocator, m_buffer, m_allocation));
	}
	m_allocator = VK_NULL_HANDLE;
	m_buffer = VK_NULL_HANDLE;
	m_allocation = VK_NULL_HANDLE;
	m_size = 0;
	m_bufferInfo = VkBufferCreateInfo();
	m_allocInfo = {};
}

vk::Buffer VmaBuffer::GetHandle() const { return m_buffer; }

VKCore::VulkanDevice* VKWrapper::VmaBuffer::GetDevice() const { return m_device; }

VmaAllocation VmaBuffer::GetAllocation() const { return m_allocation; }

vk::DeviceSize VmaBuffer::GetSize() const { return m_size; }

vk::BufferCreateInfo VmaBuffer::GetBufferInfo() const { return m_bufferInfo; }

VmaAllocationCreateInfo VmaBuffer::GetVmaAllocationInfo() const { return m_allocInfo; }

VmaBuffer::operator vk::Buffer() const { return m_buffer; }

VmaBuffer::operator VkBuffer() const { return VkBuffer(m_buffer); }

void* VmaBuffer::Map() {
	void* data;
	vmaMapMemory(m_allocator, m_allocation, &data);
	return data;
}

void VmaBuffer::Unmap() {
	vmaUnmapMemory(m_allocator, m_allocation);
}

bool VmaBuffer::Update(const void* data, size_t size, size_t offset)
{
	if (size == 0 || data == nullptr) return true;
	if (offset + size > m_size) return false;

	if (IsMappable())
		return UpdateMappable(data, size, offset); 	// 直接 CPU 写入

	auto cmd = VKCONTEXT->GetCommandBuffer();
	bool result = UpdateStagingBuffer(cmd, data, size, offset);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	return result;
}

bool VmaBuffer::UpdateAsync(std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset)
{
	if (!cmd) return false;
	if (size == 0 || data == nullptr) return true;
	if (offset + size > m_size) return false;

	if (IsMappable())
		return UpdateMappable(data, size, offset); 	// 直接 CPU 写入
	return UpdateStagingBuffer(cmd, data, size, offset);
}

bool VmaBuffer::Readback(void* outData, size_t size, size_t offset)
{
	if (outData == nullptr || size == 0) return true;
	if (offset + size > m_size) return false;

	// Buffer 可映射（HOST_VISIBLE）, 直接 CPU 读取
	if (IsMappable()) {
		return ReadMappable(outData, size, offset);
	}

	// Buffer 不可映射（DEVICE_LOCAL）, 需要 Staging Buffer 中转
	auto cmd = VKCONTEXT->GetCommandBuffer();
	bool result = ReadStagingBuffer(cmd, outData, size, offset);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	return result;
}

bool VmaBuffer::Readback(std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset)
{
	if (!cmd) return false;
	if (outData == nullptr || size == 0) return true;
	if (offset + size > m_size) return false;

	// Buffer 可映射（HOST_VISIBLE）, 直接 CPU 读取
	if (IsMappable()) {
		if (cmd->IsRecording())
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
		return ReadMappable(outData, size, offset);
	}

	// Buffer 不可映射（DEVICE_LOCAL）, 需要 Staging Buffer 中转
	return ReadStagingBuffer(cmd, outData, size, offset);
}

bool VmaBuffer::IsHostVisible() const
{
	if (!m_allocator || !m_allocation || !m_device) return false;

	VmaAllocationInfo info;
	vmaGetAllocationInfo(m_allocator, m_allocation, &info);

	const VkPhysicalDeviceMemoryProperties& memProps = m_device->GetPhysicalDeviceMemoryProperties();
	VkMemoryPropertyFlags flags = memProps.memoryTypes[info.memoryType].propertyFlags;

	return (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool VmaBuffer::IsHostCoherent() const
{
	if (!m_allocator || !m_allocation || !m_device) return false;

	VmaAllocationInfo info;
	vmaGetAllocationInfo(m_allocator, m_allocation, &info);

	const VkPhysicalDeviceMemoryProperties& memProps = m_device->GetPhysicalDeviceMemoryProperties();
	VkMemoryPropertyFlags flags = memProps.memoryTypes[info.memoryType].propertyFlags;

	return (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

bool VmaBuffer::IsDeviceLocal() const
{
	if (!m_allocator || !m_allocation || !m_device) return false;

	VmaAllocationInfo info;
	vmaGetAllocationInfo(m_allocator, m_allocation, &info);

	const VkPhysicalDeviceMemoryProperties& memProps = m_device->GetPhysicalDeviceMemoryProperties();
	VkMemoryPropertyFlags flags = memProps.memoryTypes[info.memoryType].propertyFlags;

	return (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

bool VmaBuffer::IsMappable() const
{
	return IsHostVisible();
}

bool VmaBuffer::UpdateMappable(const void* data, size_t size, size_t offset)
{
	void* mapped;
	if (vmaMapMemory(m_allocator, m_allocation, &mapped) != VK_SUCCESS) {
		return false;
	}
	memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);

	// 非 Coherent 需要 Flush
	if (!IsHostCoherent()) {
		VmaAllocationInfo info;
		vmaGetAllocationInfo(m_allocator, m_allocation, &info);

		VkMappedMemoryRange range = {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = info.deviceMemory;
		range.offset = offset;
		range.size = size;
		vkFlushMappedMemoryRanges(m_device->GetHandle(), 1, &range);
	}

	vmaUnmapMemory(m_allocator, m_allocation);
	return true;
}

bool VmaBuffer::UpdateStagingBuffer(std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset)
{
	// 创建临时 Staging Buffer
	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;

	VkBufferCreateInfo stagingInfo = {};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = size;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	if (vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS)
		return false;

	// 写入 Staging Buffer
	void* mapped;
	if (vmaMapMemory(m_allocator, stagingAllocation, &mapped) != VK_SUCCESS) {
		vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
		return false;
	}
	memcpy(mapped, data, size);

	// Staging Buffer 通常是 CPU_ONLY，可能是非 Coherent
	// 检查 staging 是否 Coherent
	VmaAllocationInfo stagingAllocationInfo;
	vmaGetAllocationInfo(m_allocator, stagingAllocation, &stagingAllocationInfo);

	const VkPhysicalDeviceMemoryProperties& memProps = m_device->GetPhysicalDeviceMemoryProperties();
	VkMemoryPropertyFlags stagingFlags = memProps.memoryTypes[stagingAllocationInfo.memoryType].propertyFlags;

	if (!(stagingFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		VkMappedMemoryRange range = {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = stagingAllocationInfo.deviceMemory;
		range.offset = 0;
		range.size = size;
		vkFlushMappedMemoryRanges(m_device->GetHandle(), 1, &range);
	}

	vmaUnmapMemory(m_allocator, stagingAllocation);

	// 拷贝到目标 Buffer
	vk::BufferCopy region;
	region.setSrcOffset(0)
		.setDstOffset(offset)
		.setSize(size);

	cmd->copyBuffer(stagingBuffer, m_buffer, region);

	// 销毁 Staging Buffer
	VKCONTEXT->Retire(new RestireBuffer(m_allocator, stagingBuffer, stagingAllocation));

	return true;
}

bool VmaBuffer::ReadMappable(void* outData, size_t size, size_t offset)
{
	void* mapped;
	if (vmaMapMemory(m_allocator, m_allocation, &mapped) != VK_SUCCESS)
		return false;

	// 非 Coherent 需要 Invalidate（让 CPU 缓存失效，从物理内存重新读取）
	if (!IsHostCoherent()) {
		VmaAllocationInfo info;
		vmaGetAllocationInfo(m_allocator, m_allocation, &info);

		VkMappedMemoryRange range = {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = info.deviceMemory;
		range.offset = offset;
		range.size = size;
		vkInvalidateMappedMemoryRanges(m_device->GetHandle(), 1, &range);
	}

	memcpy(outData, static_cast<const uint8_t*>(mapped) + offset, size);
	vmaUnmapMemory(m_allocator, m_allocation);
	return true;
}

bool VmaBuffer::ReadStagingBuffer(std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset)
{

	// 创建 Staging Buffer（CPU 可读）
	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;

	VkBufferCreateInfo stagingInfo = {};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = size;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;  // 用于读回

	if (vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
		return false;
	}

	// 拷贝：显存 → Staging Buffer
	vk::BufferCopy region = {};
	region.srcOffset = offset;
	region.dstOffset = 0;
	region.size = size;

	cmd->copyBuffer(m_buffer, stagingBuffer, region);
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	void* mapped;
	if (vmaMapMemory(m_allocator, stagingAllocation, &mapped) != VK_SUCCESS) {
		vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
		return false;
	}

	// 非 Coherent 需要 Invalidate
	VmaAllocationInfo stagingAllocationInfo;
	vmaGetAllocationInfo(m_allocator, stagingAllocation, &stagingAllocationInfo);
	const VkPhysicalDeviceMemoryProperties& memProps = m_device->GetPhysicalDeviceMemoryProperties();
	VkMemoryPropertyFlags stagingFlags = memProps.memoryTypes[stagingAllocationInfo.memoryType].propertyFlags;

	if (!(stagingFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		vk::MappedMemoryRange range;
		range.setMemory(stagingAllocationInfo.deviceMemory)
			.setOffset(0)
			.setSize(size);
		m_device->GetHandle().invalidateMappedMemoryRanges(1, &range);
	}

	memcpy(outData, mapped, size);
	vmaUnmapMemory(m_allocator, stagingAllocation);

	// 销毁 Staging Buffer
	VKCONTEXT->Retire(new RestireBuffer(m_allocator, stagingBuffer, stagingAllocation));

	return true;
}

bool VmaBuffer::CopyBuffer(VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset, size_t dstOffset)
{
	if (srcOffset + size > src.GetSize() || dstOffset + size > dst.GetSize())
		return false;

	if (&src == &dst && srcOffset == dstOffset)
		return true;

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd) return false;
	bool result = CopyBufferAsync(cmd, src, dst, size, srcOffset, dstOffset);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return result;
}

bool VKWrapper::VmaBuffer::CopyBufferAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset, size_t dstOffset)
{
	if (srcOffset + size > src.GetSize() || dstOffset + size > dst.GetSize())
		return false;

	if (&src == &dst && srcOffset == dstOffset)
		return true;

	if (!cmd) return false;

	if (&src != &dst)
	{
		vk::BufferCopy copyRegion;
		copyRegion.setSrcOffset(srcOffset)
			.setDstOffset(dstOffset)
			.setSize(size);
		cmd->copyBuffer(src.GetHandle(), dst.GetHandle(), copyRegion);
	}
	else
	{
		VKWrapper::VmaBuffer tempBuffer(src.GetDevice(), size, VmaBuffer::Usage::None, true);

		vk::BufferCopy copyRegion1;
		copyRegion1.setSrcOffset(srcOffset)
			.setDstOffset(0)
			.setSize(size);

		cmd->copyBuffer(src, tempBuffer, copyRegion1);

		{
			// 确保第一次拷贝完成后再开始第二次
			vk::BufferMemoryBarrier barrier;
			barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits::eTransferRead)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setBuffer(tempBuffer.GetHandle())  // 临时缓冲区
				.setOffset(0)
				.setSize(size);

			cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, barrier, vk::DependencyFlagBits::eByRegion);
		}

		vk::BufferCopy copyRegion2;
		copyRegion1.setSrcOffset(0)
			.setDstOffset(dstOffset)
			.setSize(size);

		// 临时缓冲区 → 原缓冲区
		cmd->copyBuffer(tempBuffer, dst, copyRegion2);

		//{
		//	vk::BufferMemoryBarrier barrier;
		//	barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
		//		.setDstAccessMask(vk::AccessFlagBits::eTransferRead)
		//		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		//		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		//		.setBuffer(dst.GetHandle())  // 临时缓冲区
		//		.setOffset(0)
		//		.setSize(size);
		//	cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, barrier, vk::DependencyFlagBits::eByRegion);
		//}
	}

	return true;
}
