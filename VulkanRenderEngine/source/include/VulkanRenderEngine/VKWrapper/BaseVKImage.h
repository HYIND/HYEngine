#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VulkanRenderEngine/General/ImageLayoutWrapper.h"
#include "VKCommandBuffer.h"

namespace VKWrapper {

	struct SubresourceState {
		vk::ImageLayout layout = vk::ImageLayout::eUndefined;
		vk::AccessFlags accessMask = vk::AccessFlagBits::eNone;
	};

	struct ImageState
	{
		mutable std::unordered_map<uint64_t, SubresourceState> _subresourceStates;

		static uint64_t MakeKey(uint32_t mipLevel, uint32_t arrayLayer = 0);
		SubresourceState& GetSubresourceState(uint32_t mipLevel, uint32_t arrayLayer = 0) const;
	};

	class BaseVKImage;

	class PassImageStateRecord
	{
	public:
		static void StartRecord();
		static void AddState(std::shared_ptr<const BaseVKImage> image, ImageLayout::BindStage stage, ImageLayout::BindUsage usage);
		static void EndRecord(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);
		static std::shared_ptr<PassImageStateRecord> Current();

	public:
		SubresourceState& GetRecordState(std::shared_ptr<const BaseVKImage> image, uint32_t level);

	private:
		std::unordered_map<std::shared_ptr<const BaseVKImage>, ImageState> _recordStates;
		std::unordered_map<std::shared_ptr<const BaseVKImage>, ImageState> _recordFirstStates;
	};

	class BaseVKImage :public std::enable_shared_from_this<BaseVKImage>
	{
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

		virtual void Release();

	private:
		SubresourceState& GetSubresourceState(uint32_t level) const;

	protected:
		VKCore::VulkanDevice* m_device = nullptr;
		vk::Image m_image = VK_NULL_HANDLE;

		uint32_t m_mipLevels = 1;
		vk::Extent3D m_extent = vk::Extent3D();
		vk::Format m_format = vk::Format::eUndefined;

		mutable ImageState m_imageState;

		friend class PassImageStateRecord;
	};

} // namespace VKWrapper