#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"

#include "CriticalSectionLock.h"
#include "SpinLock.h"

class VKWrapper::VKFence;

struct WaitSemaphoreData
{
	std::shared_ptr<VKWrapper::VKSemaphore> semaphore;
	VkPipelineStageFlags flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
};

struct CmdSyncSeamphore
{
	std::vector<WaitSemaphoreData> waitSemaphores;
	std::vector<std::shared_ptr<VKWrapper::VKSemaphore>> signalSemaphores;
};

class VKThreadContext
{
public:
	std::shared_ptr<VKWrapper::VKCommandBuffer> GetCommandBuffer();
	std::shared_ptr<VKWrapper::VKCommandPool> GetCommandPool();
	void NeedCommandPool();

private:
	std::shared_ptr<VKWrapper::VKCommandPool> _commandPool;
};

class VKContext
{

public:
	static VKContext* Instance();

	void SetInstance(std::shared_ptr<VKCore::VulkanInstance> instance);
	void SetDevice(std::shared_ptr<VKCore::VulkanDevice> device);


	std::shared_ptr<VKCore::VulkanInstance> GetInstance();
	std::shared_ptr<VKCore::VulkanDevice>  GetDevice();
	VkInstance GetInstanceHandle();
	vk::Device GetDeviceHandle();
	VmaAllocator GetVmaAllocator();
	vk::DescriptorPool GetDescriptorPool();

	std::shared_ptr<VKWrapper::VKCommandBuffer> GetCommandBuffer();

public:
	void Retire(VKWrapper::IVKResource* res);
	void ProcessRetire();

public:
	void SubmitCommandBufferToPendingQueue(std::shared_ptr<VKWrapper::VKCommandBuffer> buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
	void SubmitCommandBufferToPendingQueue(std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);

	vk::Result SubmitCommandImmediately(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
	vk::Result SubmitCommandImmediately(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);

	vk::Result SubmitCommandImmediatelyAndWait(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore = {});
	vk::Result SubmitCommandImmediatelyAndWait(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffer, const CmdSyncSeamphore& syncSeamphore = {});

	void ProcessPendingCommandAndWait();

private:
	VKContext();
	~VKContext();

	void NeedDescriptorPool();

private:
	struct SubmitCMDData
	{
		std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> buffers;
		CmdSyncSeamphore syncSeamphore;
		std::shared_ptr<VKWrapper::VKFence> signalFence;
	};

private:
	std::shared_ptr<VKCore::VulkanInstance> _instance;
	std::shared_ptr<VKCore::VulkanDevice> _device;
	vk::DescriptorPool _descriptorPool;

	std::vector<SubmitCMDData> _pendingCommandBuffers;
	SpinLock _pendingCommandBufferMutex;
	SpinLock _submitQueueMutex;

	SpinLock _descriptorPoolCreateMutex;

	std::queue<VKWrapper::IVKResource*> _pendingDestoryResource;
	SpinLock _pendingDestoryResourceMutex;
};

#define VKCONTEXT VKContext::Instance()
