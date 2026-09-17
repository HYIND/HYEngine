#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"
#include "VulkanRenderEngine\GlobalConfig.h"

#include "CriticalSectionLock.h"
#include "SpinLock.h"

class VKWrapper::VKFence;

struct WaitSemaphoreData
{
	std::shared_ptr<VKWrapper::VKSemaphore> semaphore;
	vk::PipelineStageFlags flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	uint64_t value = 0;
};

struct SignalSemaphoreData
{
	std::shared_ptr<VKWrapper::VKSemaphore> semaphore;
	uint64_t value = 0;
};

struct CmdSyncSeamphore
{
	std::vector<WaitSemaphoreData> waitSemaphores;
	std::vector<SignalSemaphoreData> signalSemaphores;
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
	vk::ResultValue<std::vector<vk::DescriptorSet>> AllocateDescriptorSets(vk::DescriptorSetAllocateInfo& allocInfo);
	vk::Result FreeDescriptorSets(const std::vector<vk::DescriptorSet>& sets);

public:
	void Retire(VKWrapper::IVKResource* res);
	void ProcessRetireAndPushFrameIndex();

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

	struct PendingResource
	{
		VKWrapper::IVKResource* res = nullptr;
		uint32_t frameIndex = 0;
	};

private:
	std::shared_ptr<VKCore::VulkanInstance> _instance;
	std::shared_ptr<VKCore::VulkanDevice> _device;

	std::vector<SubmitCMDData> _pendingCommandBuffers;
	SpinLock _pendingCommandBufferMutex;
	SpinLock _submitQueueMutex;

	vk::DescriptorPool _descriptorPool;
	SpinLock _descriptorPoolCreateMutex;
	SpinLock _descriptorPoolRequestMutex;

	std::list<PendingResource> _pendingDestoryResource;
	SpinLock _pendingDestoryResourceMutex;
	std::atomic<uint32_t> _frameIndex = 0;
};

#define VKCONTEXT VKContext::Instance()
