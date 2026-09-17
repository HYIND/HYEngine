#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VKCommandBuffer.h"

namespace VKWrapper {

	class BaseVKImage
	{
	protected:
		struct SubresourceState {
			vk::ImageLayout layout = vk::ImageLayout::eUndefined;
			vk::AccessFlags accessMask = vk::AccessFlagBits::eNone;
		};

		uint64_t MakeKey(uint32_t mipLevel, uint32_t arrayLayer = 0) const;
		SubresourceState& GetSubresourceState(uint32_t mipLevel, uint32_t arrayLayer = 0) const;

	public:
		static bool IsColorFormat(vk::Format format);
		static bool IsDepthStencilFormat(vk::Format format);
		static bool IsDepthFormat(vk::Format format);
		static bool IsStencilFormat(vk::Format format);
		static bool IsLinearFormat(vk::Format format);
		static vk::AccessFlags GetAccessMaskForLayout(vk::ImageLayout layout, vk::PipelineStageFlags dstStageMask);
		static vk::PipelineStageFlags AccessMaskToStage(vk::AccessFlags mask);
		static vk::ImageAspectFlags GetAspectMask(vk::Format format);

	public:
		BaseVKImage() = default;
		virtual ~BaseVKImage() = default;

		BaseVKImage(const BaseVKImage&) = delete;
		BaseVKImage& operator=(const BaseVKImage&) = delete;

		vk::Image GetHandle() const;
		vk::ImageLayout GetCurrentLayout(uint32_t level = 0) const;
		uint32_t GetMipLevels() const;
		vk::Extent3D GetExtent() const;
		vk::Format GetFormat() const;
		operator vk::Image() const;

		// 布局切换
		void TransitionLayout(
			std::shared_ptr<VKCommandBuffer> cmd,
			vk::ImageLayout newLayout,
			vk::PipelineStageFlags dstStageMask,
			uint32_t baseMipLevel = 0,
			uint32_t levelCount = vk::RemainingMipLevels,
			bool force = false
		);

		bool UploadData(
			const void* data,
			uint32_t mipLevel = 0,
			uint32_t layer = 0
		);

		bool GenerateMipmaps(
			std::shared_ptr<VKCommandBuffer> cmd,
			vk::ImageLayout finalLayout,
			vk::PipelineStageFlags dstStageMask
		);


	protected:
		VKCore::VulkanDevice* m_device = nullptr;
		vk::Image m_image = VK_NULL_HANDLE;

		vk::ImageAspectFlags m_aspectMask = vk::ImageAspectFlagBits::eColor;

		uint32_t m_mipLevels = 1;
		vk::Extent3D m_extent = vk::Extent3D();
		vk::Format m_format = vk::Format::eUndefined;

		// 以 (mipLevel, arrayLayer) 为键
		mutable std::unordered_map<uint64_t, SubresourceState> _subresourceStates;
	};

} // namespace VKWrapper