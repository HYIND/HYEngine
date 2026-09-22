#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "SpinLock.h"

namespace VKWrapper
{
	class VKCommandBuffer;

	class VKCommandPool :public std::enable_shared_from_this<VKCommandPool>
	{
		class CmdResPool
		{

		public:
			CmdResPool(uint32_t maxResNum = 400);
			void Clear();
			VkCommandBuffer FetchHandle();				// 分配
			bool RecycleHandle(VkCommandBuffer handle);	// 回收

		private:
			std::unordered_set<VkCommandBuffer> _iDleList;
			std::unordered_set<VkCommandBuffer> _datas;
			uint32_t _maxResNum;
		};

	public:
		enum class QueueFamilyType { Graphics = 0, Present, Compute };

	public:
		VKCommandPool(VKCore::VulkanDevice* device, QueueFamilyType type = QueueFamilyType::Graphics, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		~VKCommandPool();

		vk::Result Create(VKCore::VulkanDevice* device, QueueFamilyType type = QueueFamilyType::Graphics, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		void Release();


		vk::CommandPool GetHandle() const;
		SpinLock& GetCommandPoolMutex();

		vk::Result AllocateBuffers(std::shared_ptr<VKCommandBuffer>& buffer);
		void FreeBuffers(vk::CommandBuffer handle);
		void Trim(vk::CommandPoolTrimFlags flags = {});

	private:
		vk::CommandPool _handle = VK_NULL_HANDLE;
		VKCore::VulkanDevice* _device = nullptr;
		QueueFamilyType _queueType;
		uint32_t _queueFamilyIndex = 0;
		SpinLock _commandPoolMutex;
		CmdResPool _cmdResPool;
	};
}