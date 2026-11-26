#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "CriticalSectionLock.h"

using namespace VKWrapper;

// ======================== 上传堆 ========================

namespace
{
	// 延迟销毁要等 2 × MaxFramesInFlight 帧才回收，堆要装得下这几帧的上传量；装不下的回退到单独建 staging
	constexpr vk::DeviceSize kUploadHeapSize = 32ull * 1024 * 1024;
	// 单次超过这个大小（大贴图）也单独建 staging，免得一次就把堆占掉一大块
	constexpr vk::DeviceSize kUploadHeapMaxAllocation = kUploadHeapSize / 4;
}

class RetireUploadHeapAllocation : public IVKResource
{
public:
	explicit RetireUploadHeapAllocation(const UploadHeap::Allocation& allocation)
		:_allocation(allocation) {}
	virtual void Destroy() {
		UploadHeap::Instance().Free(_allocation);
	}

private:
	UploadHeap::Allocation _allocation;
};

UploadHeap& UploadHeap::Instance()
{
	// 不析构：缓冲由 Release（设备销毁前的回调）释放，进程退出时设备早没了
	static UploadHeap* instance = new UploadHeap();
	return *instance;
}

// 调用方持有 _mutex
bool UploadHeap::NeedLocked()
{
	if (_block)
		return true;
	if (_initTried)
		return false;
	_initTried = true;

	auto device = VKCONTEXT->GetDevice();
	if (!device || VKCONTEXT->IsShutdown())
		return false;
	VmaAllocator allocator = device->GetAllocator();

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = kUploadHeapSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// 只顺序写、不读：放系统内存（PREFER_HOST），常驻映射
	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo = {};
	if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &allocationInfo) != VK_SUCCESS || !allocationInfo.pMappedData)
	{
		if (buffer)
			vmaDestroyBuffer(allocator, buffer, allocation);
		std::cerr << std::format("[ UploadHeap ] WARNING: 创建上传堆失败（{} MB），上传全部走单独的 staging\n", kUploadHeapSize >> 20);
		return false;
	}

	VmaVirtualBlockCreateInfo blockInfo = {};
	blockInfo.size = kUploadHeapSize;
	VmaVirtualBlock block = VK_NULL_HANDLE;
	if (vmaCreateVirtualBlock(&blockInfo, &block) != VK_SUCCESS)
	{
		vmaDestroyBuffer(allocator, buffer, allocation);
		std::cerr << "[ UploadHeap ] WARNING: 创建虚拟块失败，上传全部走单独的 staging\n";
		return false;
	}

	VkMemoryPropertyFlags memoryFlags = 0;
	vmaGetAllocationMemoryProperties(allocator, allocation, &memoryFlags);

	_allocator = allocator;
	_buffer = buffer;
	_allocation = allocation;
	_mapped = static_cast<uint8_t*>(allocationInfo.pMappedData);
	_coherent = (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
	_block = block;

	VKCONTEXT->AddShutdownCallback([this] { Release(); });
	return true;
}

bool UploadHeap::Allocate(vk::DeviceSize size, vk::DeviceSize alignment, Allocation& allocation)
{
	if (size == 0 || size > kUploadHeapMaxAllocation)
		return false;

	LockGuard guard(_mutex);
	if (!NeedLocked())
		return false;

	VmaVirtualAllocationCreateInfo createInfo = {};
	createInfo.size = size;
	createInfo.alignment = alignment;

	VmaVirtualAllocation handle = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	if (vmaVirtualAllocate(_block, &createInfo, &handle, &offset) != VK_SUCCESS)
	{
		if (!_fullReported.exchange(true, std::memory_order_relaxed))
			std::cerr << std::format("[ UploadHeap ] WARNING: 上传堆满了（{} MB，本次 {} 字节），装不下的上传回退到单独建 staging（只提示一次）\n",
				kUploadHeapSize >> 20, static_cast<uint64_t>(size));
		return false;
	}

	allocation.buffer = vk::Buffer(_buffer);
	allocation.offset = offset;
	allocation.size = size;
	allocation.mapped = _mapped + offset;
	allocation.handle = handle;
	return true;
}

void UploadHeap::Flush(const Allocation& allocation)
{
	if (!allocation.handle || _coherent)
		return;
	LockGuard guard(_mutex);
	if (_allocation)
		vmaFlushAllocation(_allocator, _allocation, allocation.offset, allocation.size);
}

void UploadHeap::Free(const Allocation& allocation)
{
	if (!allocation.handle)
		return;
	LockGuard guard(_mutex);
	// 已经 Release：虚拟块整个清掉了，不用再还
	if (_block)
		vmaVirtualFree(_block, allocation.handle);
}

void UploadHeap::Retire(const Allocation& allocation)
{
	if (!allocation.handle)
		return;
	VKCONTEXT->Retire(new RetireUploadHeapAllocation(allocation));
}

void UploadHeap::Release()
{
	LockGuard guard(_mutex);
	_initTried = true;
	if (_block)
	{
		// 还没回收的段（延迟销毁队列里的）随虚拟块一起清掉；它们之后的 Free 看到 _block 为空直接返回
		vmaClearVirtualBlock(_block);
		vmaDestroyVirtualBlock(_block);
		_block = VK_NULL_HANDLE;
	}
	if (_buffer)
	{
		vmaDestroyBuffer(_allocator, _buffer, _allocation);
		_buffer = VK_NULL_HANDLE;
		_allocation = VK_NULL_HANDLE;
	}
	_mapped = nullptr;
	_allocator = VK_NULL_HANDLE;
}

// ======================== VmaBuffer ========================

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
	if (!cmd)
		return false;

	UploadHeap::Allocation upload;
	if (RecordUploadFromHeap(cmd, data, size, offset, upload))
	{
		// 等过 fence，GPU 已经读完这一段：直接还给上传堆，不用等延迟销毁
		const bool submitted = VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd) == vk::Result::eSuccess;
		if (submitted)
			UploadHeap::Instance().Free(upload);
		else
			UploadHeap::Instance().Retire(upload);
		return submitted;
	}

	bool result = UpdateStagingBuffer(cmd, data, size, offset);
	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();
	return result;
}

bool VmaBuffer::UpdateAsync(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset)
{
	if (!cmd) return false;
	if (size == 0 || data == nullptr) return true;
	if (offset + size > m_size) return false;

	if (IsMappable())
		return UpdateMappable(data, size, offset); 	// 直接 CPU 写入

	UploadHeap::Allocation upload;
	if (RecordUploadFromHeap(cmd, data, size, offset, upload))
	{
		// 命令还没提交：交给延迟销毁，和原来单独建的 staging 释放时机一样
		UploadHeap::Instance().Retire(upload);
		return true;
	}
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
		cmd->SubmitNowAndWait();
	return result;
}

bool VmaBuffer::Readback(const std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset)
{
	if (!cmd) return false;
	if (outData == nullptr || size == 0) return true;
	if (offset + size > m_size) return false;

	// Buffer 可映射（HOST_VISIBLE）, 直接 CPU 读取
	if (IsMappable()) {
		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();
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

bool VmaBuffer::RecordUploadFromHeap(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset, UploadHeap::Allocation& upload)
{
	// 上传堆建在 VKContext 的设备上
	if (!m_buffer || m_device != VKCONTEXT->GetDevice().get())
		return false;

	auto& heap = UploadHeap::Instance();
	if (!heap.Allocate(size, 16, upload))
		return false;

	memcpy(upload.mapped, data, size);
	heap.Flush(upload);

	vk::BufferCopy region;
	region.setSrcOffset(upload.offset)
		.setDstOffset(offset)
		.setSize(size);
	cmd->copyBuffer(upload.buffer, m_buffer, region);
	return true;
}

bool VmaBuffer::UpdateStagingBuffer(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset)
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

bool VmaBuffer::ReadStagingBuffer(const std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset)
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
	cmd->SubmitNowAndWait();

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
		cmd->SubmitNowAndWait();

	return result;
}

bool VKWrapper::VmaBuffer::CopyBufferAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset, size_t dstOffset)
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
	}

	return true;
}
