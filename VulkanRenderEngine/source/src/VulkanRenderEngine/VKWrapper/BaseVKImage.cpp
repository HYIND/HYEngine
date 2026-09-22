#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/BaseVKImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

uint64_t VKWrapper::ImageState::MakeKey(uint32_t mipLevel, uint32_t arrayLayer) {
	return (static_cast<uint32_t>(mipLevel) << 32) | 0;
}

VKWrapper::SubresourceState& VKWrapper::ImageState::GetSubresourceState(uint32_t mipLevel, uint32_t arrayLayer) const {
	uint64_t key = MakeKey(mipLevel, 0);
	auto it = _subresourceStates.find(key);
	if (it == _subresourceStates.end()) {
		it = _subresourceStates.emplace(key, SubresourceState{}).first;
	}
	return it->second;
}

SubresourceState& BaseVKImage::GetSubresourceState(uint32_t level) const {
	if (auto ctx = PassImageStateRecord::Current()) {
		return ctx->GetRecordState(shared_from_this(), level);
	}
	else {
		return m_imageState.GetSubresourceState(level);
	}
};

vk::Image BaseVKImage::GetHandle() const { return m_image; }

vk::ImageLayout BaseVKImage::GetCurrentLayout(uint32_t level) const {
	return GetSubresourceState(level).layout;
}

uint32_t BaseVKImage::GetMipLevels() const { return m_mipLevels; }

vk::Extent3D BaseVKImage::GetExtent() const { return m_extent; }

vk::Format BaseVKImage::GetFormat() const { return m_format; }

BaseVKImage::operator vk::Image() const { return m_image; }

void BaseVKImage::TransitionLayout(
	std::shared_ptr<VKCommandBuffer> cmd,
	vk::ImageLayout newLayout,
	vk::PipelineStageFlags dstStageMask,
	uint32_t baseMipLevel,
	uint32_t levelCount,
	bool force
) {
	if (!cmd || !m_image) return;

	vk::AccessFlags newAccessMask = ImageLayout::GetAccessMaskForLayout(newLayout, dstStageMask);

	for (uint32_t level = baseMipLevel; level < std::min(m_mipLevels, baseMipLevel + levelCount); ++level)
	{
		auto& state = GetSubresourceState(level);

		if (!force && state.layout == newLayout && state.accessMask == newAccessMask)
			continue;

		vk::PipelineStageFlags srcStageMask = ImageLayout::AccessMaskToStage(state.accessMask);

		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.setAspectMask(ImageLayout::GetAspectMaskForFormat(m_format));
		subresourceRange.setBaseMipLevel(level);
		subresourceRange.setLevelCount(1);
		subresourceRange.setBaseArrayLayer(0);
		subresourceRange.setLayerCount(vk::RemainingArrayLayers);

		vk::ImageMemoryBarrier barrier;
		barrier.setImage(m_image);
		barrier.setOldLayout(state.layout);
		barrier.setNewLayout(newLayout);
		barrier.setSrcAccessMask(state.accessMask);
		barrier.setDstAccessMask(newAccessMask);
		barrier.setSubresourceRange(subresourceRange);

		cmd->pipelineBarrier(srcStageMask, dstStageMask, barrier, vk::DependencyFlagBits::eByRegion);

		state.layout = newLayout;
		state.accessMask = newAccessMask;
	}
}

bool BaseVKImage::UploadData(const void* data, uint32_t mipLevel, uint32_t layer) {
	if (!data || !m_image) {
		return false;
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return false;

	cmd->Begin();


	// 计算像素大小（根据格式）
	size_t bytesPerPixel = ImageLayout::GetFormatSize(m_format);
	vk::DeviceSize dataSize = m_extent.width * m_extent.height * bytesPerPixel;

	// 创建 Staging Buffer（复用 VmaBuffer）
	VmaBuffer stagingBuffer;
	vk::BufferCreateInfo stagingInfo = {};
	stagingInfo.setSize(dataSize)
		.setUsage(vk::BufferUsageFlagBits::eTransferSrc);

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	if (!stagingBuffer.Create(m_device, stagingInfo, stagingAllocInfo)) {
		return false;
	}

	// 写入数据到 Staging Buffer
	void* mapped = stagingBuffer.Map();
	if (!mapped) {
		stagingBuffer.Destroy();
		return false;
	}
	memcpy(mapped, data, static_cast<size_t>(dataSize));
	stagingBuffer.Unmap();

	// 切换布局到 TRANSFER_DST（只切当前 Mip）
	TransitionLayout(
		cmd,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		mipLevel,
		1
	);

	// 执行拷贝
	vk::ImageSubresourceLayers subresourceLayers;
	subresourceLayers.setAspectMask(ImageLayout::GetAspectMaskForFormat(m_format));
	subresourceLayers.setMipLevel(mipLevel);
	subresourceLayers.setBaseArrayLayer(layer);
	subresourceLayers.setLayerCount(1);

	vk::BufferImageCopy region;
	region.setBufferOffset(0);
	region.setBufferRowLength(0);		// 0 表示紧密排列
	region.setBufferImageHeight(0);     // 0 表示紧密排列
	region.setImageSubresource(subresourceLayers);
	region.setImageOffset({ 0,0,0 });
	region.setImageExtent(m_extent);

	cmd->copyBufferToImage(stagingBuffer, m_image, vk::ImageLayout::eTransferDstOptimal, region);

	//// 切换布局到最终布局
	//TransitionLayout(
	//	cmd,
	//	finalLayout,
	//	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
	//);

	cmd->End();
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	return true;
}

void VKWrapper::BaseVKImage::Release()
{
	m_device = nullptr;
	m_image = VK_NULL_HANDLE;

	m_imageState._subresourceStates.clear();

	m_mipLevels = 1;
	m_extent = vk::Extent3D();
	m_format = vk::Format::eUndefined;
}

thread_local std::shared_ptr<PassImageStateRecord> _record;

void VKWrapper::PassImageStateRecord::StartRecord()
{
	_record = std::make_shared<PassImageStateRecord>();
}

void VKWrapper::PassImageStateRecord::AddState(std::shared_ptr<const BaseVKImage> image, ImageLayout::BindStage stage, ImageLayout::BindUsage usage)
{
	if (!_record)
		return;

	auto imageState = image->m_imageState;
	for (auto& [key, subState] : imageState._subresourceStates)
	{
		vk::ImageLayout newLayout;
		vk::PipelineStageFlags dstStageMask;
		GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, image->m_format, stage, usage);
		subState.layout = newLayout;
	}
	_record->_recordFirstStates[image] = imageState;
	_record->_recordStates[image] = imageState;
}

void VKWrapper::PassImageStateRecord::EndRecord(const std::shared_ptr<VKWrapper::VKCommandBuffer>& auxCmd)
{
	if (!_record)
		return;

	for (auto& [image, record] : _record->_recordFirstStates) {
		auto& real = image->m_imageState._subresourceStates;

		for (auto& [key, recordState] : record._subresourceStates)
		{
			auto& realState = real[key];
			uint32_t level = key >> 32;

			// 补差：真实态 → 声明态
			if (realState.layout != recordState.layout ||
				realState.accessMask != recordState.accessMask)
			{
				vk::ImageSubresourceRange subresourceRange;
				subresourceRange.setAspectMask(ImageLayout::GetAspectMaskForFormat(image->m_format));
				subresourceRange.setBaseMipLevel(level);
				subresourceRange.setLevelCount(1);
				subresourceRange.setBaseArrayLayer(0);
				subresourceRange.setLayerCount(vk::RemainingArrayLayers);

				vk::ImageMemoryBarrier barrier;
				barrier.setImage(image->GetHandle());
				barrier.setOldLayout(realState.layout);
				barrier.setNewLayout(recordState.layout);
				barrier.setSrcAccessMask(realState.accessMask);
				//barrier.setDstAccessMask(recordState.accessMask);
				barrier.setDstAccessMask(vk::AccessFlagBits::eMemoryRead);
				barrier.setSubresourceRange(subresourceRange);

				auxCmd->pipelineBarrier(
					ImageLayout::AccessMaskToStage(realState.accessMask),
					vk::PipelineStageFlagBits::eAllCommands,
					barrier,
					vk::DependencyFlagBits::eByRegion
				);
			}

			// 更新真实态为声明态（主 cmd 的起点）
			realState.layout = recordState.layout;
			realState.accessMask = recordState.accessMask;
		}
	}

	// 把本地终态写回真实态
	for (auto& [image, record] : _record->_recordStates) {
		auto& real = image->m_imageState._subresourceStates;

		for (auto& [key, recordState] : record._subresourceStates)
		{
			auto& realState = real[key];
			uint32_t level = key >> 32;
			realState.layout = recordState.layout;
			realState.accessMask = recordState.accessMask;
		}
	}

	_record.reset();
}

std::shared_ptr<PassImageStateRecord> VKWrapper::PassImageStateRecord::Current()
{
	return _record;
}

SubresourceState& VKWrapper::PassImageStateRecord::GetRecordState(std::shared_ptr<const BaseVKImage> image, uint32_t level)
{
	auto it = _recordStates.find(image);
	if (it != _recordStates.end())
	{
		auto& imageState = it->second;
		return imageState.GetSubresourceState(level);
	}
	else
	{
		_recordFirstStates[image] = image->m_imageState;
		_recordStates[image] = image->m_imageState;
		return _recordStates[image].GetSubresourceState(level);
	}
}
