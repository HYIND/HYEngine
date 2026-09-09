#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "VKRenderPass.h"

namespace VKWrapper
{

	class VKRenderPass;

	// ---------- FrameBuffer 配置 ----------
	struct FrameBufferConfig {
		VKCore::VulkanDevice* device = nullptr;
		VKRenderPass* renderPass = nullptr;
		glm::ivec2 size = glm::ivec2(1);
		std::vector<vk::ImageView> attachments;
		uint32_t layers = 1;
	};

	class VKFrameBuffer 
	{
	public:
		VKFrameBuffer() = default;
		~VKFrameBuffer();

		VKFrameBuffer(FrameBufferConfig& config);

		// 禁止拷贝，允许移动
		VKFrameBuffer(const VKFrameBuffer&) = delete;
		VKFrameBuffer& operator=(const VKFrameBuffer&) = delete;
		VKFrameBuffer(VKFrameBuffer&& other) noexcept;
		VKFrameBuffer& operator=(VKFrameBuffer&& other) noexcept;

		vk::Result Create(FrameBufferConfig& config);
		void Release();

		VKCore::VulkanDevice* GetDevice() const;
		VKRenderPass* GetRenderPass() const;
		vk::Framebuffer GetHandle() const;

	private:
		VKCore::VulkanDevice* _device = nullptr;
		VKRenderPass* _renderPass = nullptr;

		vk::Framebuffer _handle = VK_NULL_HANDLE;
	};

}