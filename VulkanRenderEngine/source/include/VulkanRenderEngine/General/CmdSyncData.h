#pragma once

#include "VulkanRenderEngine/VKWrapper/VKSemaphore.h"
#include "VulkanRenderEngine/VKWrapper/VKFence.h"

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

struct SubmitCMDData
{
	std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> buffers;
	CmdSyncSeamphore syncSeamphore;
	std::shared_ptr<VKWrapper::VKFence> signalFence;
};