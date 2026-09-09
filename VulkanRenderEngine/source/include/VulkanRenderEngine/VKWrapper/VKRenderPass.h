#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VKFrameBuffer.h"
#include "VKCommandBuffer.h"


namespace VKWrapper
{

	class VKFrameBuffer;

	struct RenderPassConfig {
		// ---------- 核心数据 ----------
		VKCore::VulkanDevice* device = nullptr;

		std::vector<vk::AttachmentDescription> attachments;
		std::vector<vk::SubpassDescription> subpasses;
		std::vector<vk::SubpassDependency> dependencies;


		// ---------- 内部辅助存储（用于构建 SubpassDescription） ----------
		std::vector<std::vector<vk::AttachmentReference>> colorRefs;
		std::vector<std::vector<vk::AttachmentReference>> depthRefs;
		std::vector<std::vector<vk::AttachmentReference>> inputRefs;
		std::vector<std::vector<vk::AttachmentReference>> resolveRefs;

		// ---------- 添加附件 ----------
		void AddAttachment(
			vk::Format format = vk::Format::eUndefined,
			vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
			vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined,
			vk::ImageLayout finalLayout = vk::ImageLayout::ePresentSrcKHR
		);

		// ---------- 添加子通道 ----------
		void AddSubpass(
			vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics,
			const std::vector<vk::AttachmentReference>& colorAttachments = {},
			const std::vector<vk::AttachmentReference>& depthStencilAttachments = {},
			const std::vector<vk::AttachmentReference>& inputAttachments = {},
			const std::vector<vk::AttachmentReference>& resolveAttachments = {}
		);

		// ---------- 添加子通道（只传附件索引） ----------
		void AddSubpass(
			vk::PipelineBindPoint bindPoint,
			const std::vector<uint32_t>& colorAttachmentIndices,
			std::optional<uint32_t> depthStencilAttachmentIndex = std::nullopt,
			const std::vector<uint32_t>& inputAttachmentIndices = {},
			const std::vector<uint32_t>& resolveAttachmentIndices = {}
		);

		// ---------- 添加依赖 ----------
		void AddDependency(
			uint32_t srcSubpass = VK_SUBPASS_EXTERNAL,
			uint32_t dstSubpass = 0,
			vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::AccessFlags srcAccessMask = vk::AccessFlagBits::eNone,
			vk::AccessFlags dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
			vk::DependencyFlags dependencyFlags = vk::DependencyFlagBits::eByRegion
		);

		std::tuple<vk::Result, vk::RenderPass> Create();

		// 默认配置 1颜色+1深度
		static RenderPassConfig CreateDefaultConfig(
			VKCore::VulkanDevice* device,
			vk::Format colorFormat,
			vk::Format depthFormat,
			vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
		);

		// 屏幕配置 仅1颜色
		static RenderPassConfig CreateSceenDefaultConfig(
			VKCore::VulkanDevice* device,
			vk::Format colorFormat,
			vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
		);



	private:
		void UpdateSubpassPointers();
	};

	class VKRenderPass {
	public:
		VKRenderPass() = default;
		~VKRenderPass();

		// 禁止拷贝，允许移动
		VKRenderPass(const VKRenderPass&) = delete;
		VKRenderPass& operator=(const VKRenderPass&) = delete;
		VKRenderPass(VKRenderPass&& other) noexcept;
		VKRenderPass& operator=(VKRenderPass&& other) noexcept;

		// ---------- 创建 ----------
		bool Create(RenderPassConfig& config);
		void Release();

		// ---------- Getter ----------
		vk::RenderPass GetHandle() const;
		VKCore::VulkanDevice* GetDevice() const;
		bool IsValid() const;

		void BeginPass(std::shared_ptr<VKCommandBuffer> commandBuffer, vk::RenderPassBeginInfo& beginInfo, vk::SubpassContents subpassContents = vk::SubpassContents::eInline) const;
		void BeginPass(std::shared_ptr<VKCommandBuffer> commandBuffer, VKFrameBuffer& framebuffer, const vk::Rect2D& renderArea, const std::vector<vk::ClearValue>& clearValues = {}, vk::SubpassContents subpassContents = vk::SubpassContents::eInline) const;
		void NextSubPass(std::shared_ptr<VKCommandBuffer> commandBuffer, vk::SubpassContents subpassContents = vk::SubpassContents::eInline) const;
		void EndPass(std::shared_ptr<VKCommandBuffer> commandBuffer) const;

	private:
		VKCore::VulkanDevice* m_device = nullptr;
		vk::RenderPass m_renderPass = VK_NULL_HANDLE;
	};

}