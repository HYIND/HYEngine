#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"
#include "VulkanRenderEngine/General/CmdSyncData.h"
#include "VulkanRenderEngine\GlobalConfig.h"

#include "CriticalSectionLock.h"
#include "SpinLock.h"

#include <atomic>
#include <functional>
#include <unordered_set>

class VKWrapper::VKFence;

// 每个线程一份（thread_local），持有这个线程的命令池。
// 线程结束时析构；主线程的要等 main 返回后、CRT 退出阶段才析构。所以命令池登记在 VKContext 里，
// VKContext::Shutdown 销毁设备前会统一释放，之后线程再析构就什么都不用做
class VKThreadContext
{
public:
	VKThreadContext() = default;
	~VKThreadContext();
	VKThreadContext(const VKThreadContext&) = delete;
	VKThreadContext& operator=(const VKThreadContext&) = delete;

	std::shared_ptr<VKWrapper::VKCommandBuffer> GetCommandBuffer();
	bool GetCommandBuffer(std::shared_ptr<VKWrapper::VKCommandBuffer> existedCmdInstance);
	std::shared_ptr<VKWrapper::VKCommandPool> GetCommandPool();
	void NeedCommandPool();

private:
	friend class VKContext;
	// VKContext::Shutdown 调用（持有登记表的锁）：释放命令池
	void ReleaseCommandPool() { _commandPool.reset(); }

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
	void FlushRetiredResources();

public:
	// 引擎持有设备和实例，由这里统一销毁。调用前必须先停掉所有会用设备的东西：
	// 引擎（EngineRuntime::Shutdown）、宿主自己的 Vulkan 对象（ImGui 后端、交换链、surface）。
	// 依次：等 GPU 空闲 → 清空延迟销毁队列 → 全局缓存释放（AddShutdownCallback 登记的，倒序）→ 再清空 →
	// 各线程的命令池 → 描述符池 → 设备 → 实例。之后 GetDevice / GetInstance 返回空，Retire 直接丢弃
	void Shutdown();
	bool IsShutdown() const { return _retireClosed.load(std::memory_order_acquire); }
	// 持有 GPU 资源的全局单例（BindlessTextureManager、IndirectDrawManager 等）在这里登记释放函数
	void AddShutdownCallback(std::function<void()> callback);

public:
	void SubmitCommandBufferToPendingQueue(std::shared_ptr<VKWrapper::VKCommandBuffer> buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
	void SubmitCommandBufferToPendingQueue(std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);

	vk::Result SubmitCommandImmediately(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
	vk::Result SubmitCommandImmediately(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffer, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);

	vk::Result SubmitCommandImmediatelyAndWait(const std::shared_ptr<VKWrapper::VKCommandBuffer>& buffer, const CmdSyncSeamphore& syncSeamphore = {});
	vk::Result SubmitCommandImmediatelyAndWait(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffer, const CmdSyncSeamphore& syncSeamphore = {});

	void ProcessPendingCommand();

public:
	SpinLock& GetDescriptorPoolRequestMutex();

private:
	VKContext();
	~VKContext();

	void NeedDescriptorPool();

	// SubmitCommandImmediatelyAndWait 用的 fence：等完、重置后放回来复用，
	// 省掉每次 vkCreateFence + 延迟销毁。只回收确认没在用的（等成功并重置成功、或根本没提交出去的）
	std::shared_ptr<VKWrapper::VKFence> AcquireWaitFence();
	void RecycleWaitFence(std::shared_ptr<VKWrapper::VKFence> fence);
	// 提交并等待：成功时 fence 重置后放回池子，失败时不回收（让它走延迟销毁）
	vk::Result SubmitAndWaitWithPooledFence(const std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>>& buffers,
		const std::vector<WaitSemaphoreData>& waitSemaphores, const std::vector<SignalSemaphoreData>& signalSemaphores);

	friend class VKThreadContext;
	void RegisterThreadContext(VKThreadContext* context);
	void UnregisterThreadContext(VKThreadContext* context);

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
	std::atomic<bool> _hasPendingCommands{ false };	// 只在 _pendingCommandBufferMutex 里改；ProcessPendingCommand 不拿锁先看它
	SpinLock _submitQueueMutex;

	vk::DescriptorPool _descriptorPool;
	SpinLock _descriptorPoolCreateMutex;
	SpinLock _descriptorPoolRequestMutex;

	std::list<PendingResource> _pendingDestoryResource;
	SpinLock _pendingDestoryResourceMutex;
	std::atomic<uint32_t> _frameIndex = 0;

	// 有命令池的线程上下文
	std::unordered_set<VKThreadContext*> _threadContexts;
	SpinLock _threadContextsMutex;

	std::vector<std::shared_ptr<VKWrapper::VKFence>> _waitFencePool;	// 空闲、未置位的 fence
	SpinLock _waitFencePoolMutex;

	std::vector<std::function<void()>> _shutdownCallbacks;
	SpinLock _shutdownCallbacksMutex;
	std::atomic<bool> _retireClosed{ false };	// 设备销毁了：Retire 进来的资源直接丢弃，不再执行 Destroy
};

#define VKCONTEXT VKContext::Instance()
