#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "VulkanRenderEngine\VKWrapper\VKCommandBuffer.h"

namespace VKWrapper
{

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
		static bool CopyBufferAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, VmaBuffer& src, VmaBuffer& dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0);

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
		bool UpdateAsync(std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset);

		// 读回数据
		bool Readback(void* outData, size_t size, size_t offset = 0);
		bool Readback(std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset = 0);

		// ---------- 查询 ----------
		bool IsHostVisible() const;
		bool IsHostCoherent() const;
		bool IsDeviceLocal() const;
		bool IsMappable() const;

	private:
		bool UpdateMappable(const void* data, size_t size, size_t offset);
		bool UpdateStagingBuffer(std::shared_ptr<VKCommandBuffer>& cmd, const void* data, size_t size, size_t offset);

		bool ReadMappable(void* outData, size_t size, size_t offset);
		bool ReadStagingBuffer(std::shared_ptr<VKCommandBuffer>& cmd, void* outData, size_t size, size_t offset);

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
