#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "VulkanRenderEngine\VKWrapper\VKCommandBuffer.h"

#include <atomic>

namespace VKWrapper
{

	// 上传堆：一块常驻映射、只做传输源的 host 可见缓冲，里面的偏移交给 VMA 虚拟块（vmaVirtualAllocate）管理。
	// 缓冲和贴图上传的 staging 先从这里切一段，省掉每次 vkCreateBuffer / 映射 / vkDestroyBuffer；
	// 分配不到（单次太大或堆满了）时返回 false，调用方回退到单独建 staging。
	// 线程安全。第一次分配时创建；设备销毁前由 VKContext::Shutdown 的回调释放，之后分配一律失败
	class UploadHeap
	{
	public:
		struct Allocation
		{
			vk::Buffer buffer = VK_NULL_HANDLE;		// 上传堆的缓冲（拷贝源）
			vk::DeviceSize offset = 0;				// 本段在缓冲里的偏移
			vk::DeviceSize size = 0;
			void* mapped = nullptr;					// 本段起点的映射地址
			VmaVirtualAllocation handle = VK_NULL_HANDLE;
		};

		static UploadHeap& Instance();

		// alignment 必须是 2 的幂（0 等同 1）
		bool Allocate(vk::DeviceSize size, vk::DeviceSize alignment, Allocation& allocation);
		// 写完数据、提交命令之前调用：内存不是 HOST_COHERENT 时 flush 这一段
		void Flush(const Allocation& allocation);
		// 释放一段。已经确认 GPU 读完（等过 fence）用 Free；命令还没执行完用 Retire，交给延迟销毁
		void Free(const Allocation& allocation);
		void Retire(const Allocation& allocation);

		// 设备销毁前调用：销毁缓冲和虚拟块，之后不再创建
		void Release();

	private:
		UploadHeap() = default;
		bool NeedLocked();

	private:
		SpinLock _mutex;
		VmaAllocator _allocator = VK_NULL_HANDLE;
		VkBuffer _buffer = VK_NULL_HANDLE;
		VmaAllocation _allocation = VK_NULL_HANDLE;
		uint8_t* _mapped = nullptr;
		bool _coherent = true;
		VmaVirtualBlock _block = VK_NULL_HANDLE;
		bool _initTried = false;					// 只试着建一次：失败或释放之后都不再建
		std::atomic<bool> _fullReported{ false };	// 堆满回退只打一次日志
	};

	class VmaBuffer
	{
	public:
		enum class Usage : uint32_t {
			None = 0,
			UniformBuffer = 1 << 0,
			StorageBuffer = 1 << 1,
			VertexBuffer = 1 << 2,
			IndexBuffer = 1 << 3,
			IndirectBuffer = 1 << 4,
			SBTBuffer = 1 << 5
		};

	public:
		static bool CopyBuffer(VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0);
		static bool CopyBufferAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0);

	public:
		VmaBuffer() = default;
		VmaBuffer(VKCore::VulkanDevice* vulkanDevice, uint64_t size, Usage usage, bool cpuAccess = false);
		VmaBuffer(VKCore::VulkanDevice* vulkanDevice, const vk::BufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& allocInfo);
		~VmaBuffer();

		VmaBuffer(VmaBuffer&& other) noexcept;
		VmaBuffer& operator=(VmaBuffer&& other) noexcept;

		bool Create(VKCore::VulkanDevice* vulkanDevice, uint64_t size, Usage usage, bool cpuAccess = false);
		bool Create(VKCore::VulkanDevice* vulkanDevice, const vk::BufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& allocInfo);

		void Destroy();

		// Getter
		vk::Buffer GetHandle() const;
		VKCore::VulkanDevice* GetDevice() const;
		VmaAllocation GetAllocation() const;
		vk::DeviceSize GetSize() const;
		vk::BufferCreateInfo GetBufferInfo() const;
		VmaAllocationCreateInfo GetVmaAllocationInfo() const;

		// 隐式转换
		operator VkBuffer() const;
		operator vk::Buffer() const;

		// 映射/更新数据（快捷操作）
		void* Map();
		void Unmap();

		// ---------- 数据更新 ----------
		// 自动根据内存类型决定是否支持
		bool Update(const void* data, size_t size, size_t offset);
		bool UpdateAsync(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset);

		// 读回数据
		bool Readback(void* outData, size_t size, size_t offset = 0);
		bool Readback(const std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset = 0);

		// ---------- 查询 ----------
		bool IsHostVisible() const;
		bool IsHostCoherent() const;
		bool IsDeviceLocal() const;
		bool IsMappable() const;

	private:
		bool UpdateMappable(const void* data, size_t size, size_t offset);
		// 从上传堆切一段当 staging 并录拷贝；成功时这一段由调用方释放（Free / Retire），失败时什么都没录
		bool RecordUploadFromHeap(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset, UploadHeap::Allocation& upload);
		// 单独建一个 staging（上传堆分配不到时的回退）
		bool UpdateStagingBuffer(const std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset);

		bool ReadMappable(void* outData, size_t size, size_t offset);
		bool ReadStagingBuffer(const std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset);

	private:
		VKCore::VulkanDevice* m_device = nullptr;
		VmaAllocator m_allocator = VK_NULL_HANDLE;
		vk::Buffer m_buffer = VK_NULL_HANDLE;
		VmaAllocation m_allocation = VK_NULL_HANDLE;

		vk::DeviceSize m_size = 0;
		vk::BufferCreateInfo m_bufferInfo = {};
		VmaAllocationCreateInfo m_allocInfo = {};
	};

}

inline constexpr VKWrapper::VmaBuffer::Usage operator|(VKWrapper::VmaBuffer::Usage lhs, VKWrapper::VmaBuffer::Usage rhs) {
	return static_cast<VKWrapper::VmaBuffer::Usage>(
		static_cast<std::underlying_type_t<VKWrapper::VmaBuffer::Usage>>(lhs) |
		static_cast<std::underlying_type_t<VKWrapper::VmaBuffer::Usage>>(rhs)
		);
}

inline constexpr bool operator&(VKWrapper::VmaBuffer::Usage lhs, VKWrapper::VmaBuffer::Usage rhs) {
	return static_cast<VKWrapper::VmaBuffer::Usage>(
		static_cast<std::underlying_type_t<VKWrapper::VmaBuffer::Usage>>(lhs) &
		static_cast<std::underlying_type_t<VKWrapper::VmaBuffer::Usage>>(rhs)
		) != VKWrapper::VmaBuffer::Usage::None;
}
