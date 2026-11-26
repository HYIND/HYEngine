#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"
#include "VulkanRenderEngine/General/CmdSyncData.h"
#include "VulkanRenderEngine\GlobalConfig.h"

#include "CriticalSectionLock.h"
#include "SpinLock.h"

class VKWrapper::VKFence;

class VKThreadContext
{
public:
	std::shared_ptr<VKWrapper::VKCommandBuffer> GetCommandBuffer();
	bool GetCommandBuffer(std::shared_ptr<VKWrapper::VKCommandBuffer> existedCmdInstance);
	std::shared_ptr<VKWrapper::VKCommandPool> GetCommandPool();
	void NeedCommandPool();

private:
	std::shared_ptr<VKWrapper::VKCommandPool> _commandPool;
};

class VKContext
{

public:
	static VKContext* Instance();

	// 引擎只有一套 Vulkan 实例和设备，都放在这里；渲染器等只是使用方。
	// 建实例并设为当前实例，失败返回空
	std::shared_ptr<VKCore::VulkanInstance> CreateInstance(const std::vector<std::string>& extensionNames);
	// 在当前实例上选物理设备、建逻辑设备并设为当前设备，失败返回空。
	// extraDeviceExtensions：宿主额外要的设备扩展（例如编辑器自己的窗口交换链要 VK_KHR_swapchain），和引擎必需的重复会自动去掉
	std::shared_ptr<VKCore::VulkanDevice> CreateDevice(const std::vector<std::string>& extraDeviceExtensions = {});

	void SetInstance(std::shared_ptr<VKCore::VulkanInstance> instance);
	void SetDevice(std::shared_ptr<VKCore::VulkanDevice> device);


	std::shared_ptr<VKCore::VulkanInstance> GetInstance();
	std::shared_ptr<VKCore::VulkanDevice>  GetDevice();
	VkInstance GetInstanceHandle();
	vk::Device GetDeviceHandle();
	VmaAllocator GetVmaAllocator();
	vk::DescriptorPool GetDescriptorPool();

	std::shared_ptr<VKWrapper::VKCommandBuffer> GetCommandBuffer();
	bool GetCommandBuffer(std::shared_ptr<VKWrapper::VKCommandBuffer> existedCmdInstance);

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

public:
	SpinLock& GetDescriptorPoolRequestMutex();

private:
	VKContext();
	~VKContext();

	void NeedDescriptorPool();

private:
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
