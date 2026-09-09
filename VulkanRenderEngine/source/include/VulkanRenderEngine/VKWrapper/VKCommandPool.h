#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "SpinLock.h"

namespace VKWrapper
{
	class VKCommandBuffer;

	class VKCommandPool
	{
	public:
		enum class QueueFamilyType { Graphics = 0, Present, Compute };

	public:
		VKCommandPool(VKCore::VulkanDevice* device, QueueFamilyType type = QueueFamilyType::Graphics, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		~VKCommandPool();

		vk::Result Create(VKCore::VulkanDevice* device, QueueFamilyType type = QueueFamilyType::Graphics, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		void Release();


		vk::CommandPool GetHandle() const;
		//SpinLock& GetCommandPoolMutex();

		vk::Result AllocateBuffers(VKCommandBuffer* buffer, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
		vk::Result AllocateBuffers(std::shared_ptr<VKCommandBuffer> buffer, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
		vk::Result AllocateBuffers(std::vector<std::shared_ptr<VKCommandBuffer>>& buffers, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
		void FreeBuffers(VKCommandBuffer* buffer);
		void FreeBuffers(std::shared_ptr<VKCommandBuffer> buffer);
		void FreeBuffers(std::vector<std::shared_ptr<VKCommandBuffer>>& buffers);
		void Trim(vk::CommandPoolTrimFlags flags = {});

	private:
		vk::CommandPool _handle = VK_NULL_HANDLE;
		VKCore::VulkanDevice* _device = nullptr;
		QueueFamilyType _queueType;
		uint32_t _queueFamilyIndex = 0;
		//SpinLock _commandPoolMutex;
	};
}