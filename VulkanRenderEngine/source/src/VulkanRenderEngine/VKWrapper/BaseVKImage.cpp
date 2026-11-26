#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/BaseVKImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

template<typename F>
void BaseVKImage::VisitStates(F&& f) const
{
	// Pass 录制期间：改的是本 Pass 的记录，第一次提交时再统一补差、写回真实记录
	if (auto record = PassImageStateRecord::Current())
	{
		LockGuard guard(record->_mutex);
		f(record->GetEntryLocked(shared_from_this()).current);
		return;
	}

	LockGuard guard(m_imageState.lock);
	if (m_imageState.mips.size() < m_mipLevels)
		m_imageState.mips.resize(m_mipLevels);
	f(m_imageState.mips);
}

void BaseVKImage::ResetImageState(vk::ImageLayout initialLayout)
{
	{
		LockGuard guard(m_imageState.lock);
		m_imageState.mips.assign(m_mipLevels, SubresourceState{ initialLayout, vk::AccessFlagBits::eNone });
	}
	m_sampledImmutable.store(false, std::memory_order_release);
	m_misuseReported.store(false, std::memory_order_relaxed);
}

void BaseVKImage::ReportImmutableMisuse(const char* what) const
{
	if (!m_misuseReported.exchange(true, std::memory_order_relaxed))
		std::cerr << std::format("[ BaseVKImage ] ERROR: 只读资产贴图（VkImage {:#x}）不能 {}：布局固定为 SHADER_READ_ONLY_OPTIMAL，已忽略。"
			"要改内容请新建一张图替换\n", reinterpret_cast<uintptr_t>(static_cast<VkImage>(m_image)), what);
}

vk::Image BaseVKImage::GetHandle() const { return m_image; }

vk::ImageLayout BaseVKImage::GetCurrentLayout(uint32_t level) const {
	if (IsSampledImmutable())
		return vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::ImageLayout layout = vk::ImageLayout::eUndefined;
	VisitStates([&](std::vector<SubresourceState>& states)
		{
			if (level < states.size())
				layout = states[level].layout;
		});
	return layout;
}

uint32_t BaseVKImage::GetMipLevels() const { return m_mipLevels; }

vk::Extent3D BaseVKImage::GetExtent() const { return m_extent; }

vk::Format BaseVKImage::GetFormat() const { return m_format; }

BaseVKImage::operator vk::Image() const { return m_image; }

void BaseVKImage::TransitionStatesLocked(std::vector<SubresourceState>& states,
	const std::shared_ptr<VKCommandBuffer>& cmd, vk::ImageLayout newLayout, vk::PipelineStageFlags dstStageMask,
	uint32_t baseMipLevel, uint32_t endMipLevel, bool force) const
{
	const vk::AccessFlags newAccessMask = ImageLayout::GetAccessMaskForLayout(newLayout, dstStageMask);
	endMipLevel = std::min<uint32_t>(endMipLevel, static_cast<uint32_t>(states.size()));

	for (uint32_t level = baseMipLevel; level < endMipLevel; ++level)
	{
		auto& state = states[level];

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
		barrier
			.setImage(m_image)
			.setOldLayout(state.layout)
			.setNewLayout(newLayout)
			.setSrcAccessMask(state.accessMask)
			.setDstAccessMask(newAccessMask)
			.setSubresourceRange(subresourceRange);

		cmd->pipelineBarrier(srcStageMask, dstStageMask, barrier, vk::DependencyFlagBits::eByRegion);

		state.layout = newLayout;
		state.accessMask = newAccessMask;
	}
}

void BaseVKImage::TransitionLayout(
	std::shared_ptr<VKCommandBuffer> cmd,
	vk::ImageLayout newLayout,
	vk::PipelineStageFlags dstStageMask,
	uint32_t baseMipLevel,
	uint32_t levelCount,
	bool force
) {
	if (!cmd || !m_image) return;

	// 只读资产贴图：本来就一直是只读布局，什么都不用做；要切到别的布局一律拒绝
	if (IsSampledImmutable())
	{
		if (newLayout != vk::ImageLayout::eShaderReadOnlyOptimal)
			ReportImmutableMisuse("切换布局");
		return;
	}

	if (baseMipLevel >= m_mipLevels)
		return;

	const uint32_t endMipLevel = baseMipLevel + std::min(m_mipLevels - baseMipLevel, levelCount);

	// 读、比较、录屏障、写回在同一把锁里完成
	VisitStates([&](std::vector<SubresourceState>& states)
		{
			TransitionStatesLocked(states, cmd, newLayout, dstStageMask, baseMipLevel, endMipLevel, force);
		});
}

vk::ImageLayout BaseVKImage::TransitionLayoutForSample(
	std::shared_ptr<VKCommandBuffer> cmd,
	ImageLayout::BindStage stage,
	uint32_t baseMipLevel,
	uint32_t levelCount
) {
	// 只读资产贴图：直接用只读布局，不碰布局记录（bindless 每个 Pass 都会对所有资产贴图走一遍这里）
	if (IsSampledImmutable())
		return vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;
	ImageLayout::GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, m_format, stage, ImageLayout::BindUsage::Sample);

	if (!cmd || !m_image || baseMipLevel >= m_mipLevels)
		return newLayout;

	const uint32_t endMipLevel = baseMipLevel + std::min(m_mipLevels - baseMipLevel, levelCount);
	const bool mayKeepGeneral = newLayout != vk::ImageLayout::eGeneral
		&& (stage == ImageLayout::BindStage::Compute || stage == ImageLayout::BindStage::RayTracing);

	// 「是否全是 GENERAL」的判断和切换放在同一把锁里，中间不会被别的线程改掉
	VisitStates([&](std::vector<SubresourceState>& states)
		{
			if (mayKeepGeneral)
			{
				bool allGeneral = true;
				for (uint32_t level = baseMipLevel; level < endMipLevel && allGeneral; ++level)
					allGeneral = level < states.size() && states[level].layout == vk::ImageLayout::eGeneral;
				if (allGeneral)
					newLayout = vk::ImageLayout::eGeneral;
			}
			TransitionStatesLocked(states, cmd, newLayout, dstStageMask, baseMipLevel, endMipLevel, false);
		});
	return newLayout;
}

bool BaseVKImage::MakeSampledImmutable()
{
	if (!m_image)
		return false;
	if (IsSampledImmutable())
		return true;
	if (!ImageLayout::IsColorFormat(m_format))
	{
		std::cerr << std::format("[ BaseVKImage ] MakeSampledImmutable 只支持颜色格式（{}），已忽略\n", vk::to_string(m_format));
		return false;
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return false;

	// 直接改真实记录（不走 Pass 的记录）：所有 mip（上传完停在 TransferDst、生成 mip 后停在 TransferSrc / TransferDst）
	// 一次性切到只读，等 GPU 做完再设标记，之后谁拿到这张图都已经是只读布局
	{
		LockGuard guard(m_imageState.lock);
		if (m_imageState.mips.size() < m_mipLevels)
			m_imageState.mips.resize(m_mipLevels);
		TransitionStatesLocked(m_imageState.mips, cmd, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eAllCommands, 0, m_mipLevels, false);
	}
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	m_sampledImmutable.store(true, std::memory_order_release);
	return true;
}

bool BaseVKImage::UploadData(const void* data, uint32_t mipLevel, uint32_t layer) {
	if (!data || !m_image) {
		return false;
	}
	if (IsSampledImmutable()) {
		ReportImmutableMisuse("上传数据");
		return false;
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return false;

	cmd->Begin();


	// 计算像素大小（根据格式）。按目标 mip 的尺寸算（原来一律用 0 级尺寸，mip > 0 时拷贝范围越界）
	size_t bytesPerPixel = ImageLayout::GetFormatSize(m_format);
	const vk::Extent3D mipExtent{
		std::max(1u, m_extent.width >> mipLevel),
		std::max(1u, m_extent.height >> mipLevel),
		std::max(1u, std::max(1u, m_extent.depth) >> mipLevel)
	};
	vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(mipExtent.width) * mipExtent.height * mipExtent.depth * bytesPerPixel;

	// Staging：先从上传堆切一段。bufferOffset 要是 4 和像素大小的倍数，虚拟块的对齐只能是 2 的幂，
	// 所以像素大小不是 2 的幂（如 RGB32F）时不走上传堆；分配不到时单独建一个
	UploadHeap::Allocation upload;
	const bool texelPow2 = bytesPerPixel > 0 && (bytesPerPixel & (bytesPerPixel - 1)) == 0;
	const bool fromHeap = texelPow2 && m_device == VKCONTEXT->GetDevice().get()
		&& UploadHeap::Instance().Allocate(dataSize, std::max<vk::DeviceSize>(4, bytesPerPixel), upload);

	VmaBuffer stagingBuffer;
	vk::Buffer srcBuffer = VK_NULL_HANDLE;
	vk::DeviceSize srcOffset = 0;
	if (fromHeap)
	{
		memcpy(upload.mapped, data, static_cast<size_t>(dataSize));
		UploadHeap::Instance().Flush(upload);
		srcBuffer = upload.buffer;
		srcOffset = upload.offset;
	}
	else
	{
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
		srcBuffer = stagingBuffer.GetHandle();
	}

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
	region.setBufferOffset(srcOffset);
	region.setBufferRowLength(0);		// 0 表示紧密排列
	region.setBufferImageHeight(0);     // 0 表示紧密排列
	region.setImageSubresource(subresourceLayers);
	region.setImageOffset({ 0,0,0 });
	region.setImageExtent(mipExtent);

	cmd->copyBufferToImage(srcBuffer, m_image, vk::ImageLayout::eTransferDstOptimal, region);

	//// 切换布局到最终布局
	//TransitionLayout(
	//	cmd,
	//	finalLayout,
	//	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
	//);

	cmd->End();
	const vk::Result submitResult = VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	// 上传堆的这一段：等过 fence 就直接还回去，没等成功交给延迟销毁
	if (fromHeap)
	{
		if (submitResult == vk::Result::eSuccess)
			UploadHeap::Instance().Free(upload);
		else
			UploadHeap::Instance().Retire(upload);
	}

	return true;
}

void VKWrapper::BaseVKImage::Release()
{
	m_device = nullptr;
	m_image = VK_NULL_HANDLE;

	{
		LockGuard guard(m_imageState.lock);
		m_imageState.mips.clear();
	}
	m_sampledImmutable.store(false, std::memory_order_release);
	m_misuseReported.store(false, std::memory_order_relaxed);

	m_mipLevels = 1;
	m_extent = vk::Extent3D();
	m_format = vk::Format::eUndefined;
}

void VKWrapper::BaseVKImage::GenerateMipMaps()
{
	if (m_mipLevels <= 1)
		return;
	if (IsSampledImmutable()) {
		ReportImmutableMisuse("生成 mip");
		return;
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();

	uint32_t width = m_extent.width;
	uint32_t height = m_extent.height;
	for (uint32_t level = 1; level < m_mipLevels; level++) {
		uint32_t prevW = std::max(1u, width >> (level - 1));
		uint32_t prevH = std::max(1u, height >> (level - 1));
		uint32_t currW = std::max(1u, width >> level);
		uint32_t currH = std::max(1u, height >> level);

		uint32_t inputLevel = level - 1;
		uint32_t outputLevel = level;

		vk::ImageLayout srcLayout = GetCurrentLayout(inputLevel);
		vk::ImageLayout dstLayout = GetCurrentLayout(outputLevel);

		if (srcLayout != vk::ImageLayout::eTransferSrcOptimal) {
			TransitionLayout(
				cmd,
				vk::ImageLayout::eTransferSrcOptimal,
				vk::PipelineStageFlagBits::eTransfer,
				inputLevel,
				1
			);
		}

		if (dstLayout != vk::ImageLayout::eTransferDstOptimal) {
			TransitionLayout(
				cmd,
				vk::ImageLayout::eTransferDstOptimal,
				vk::PipelineStageFlagBits::eTransfer,
				outputLevel,
				1
			);
		}

		std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)prevW, (int)prevH, 1 } };
		std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)currW, (int)currH, 1} };

		vk::ImageBlit blitRegion = {};
		blitRegion.setSrcSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(inputLevel).setBaseArrayLayer(0).setLayerCount(m_layerCount));
		blitRegion.setSrcOffsets(srcOffsets);
		blitRegion.setDstSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(outputLevel).setBaseArrayLayer(0).setLayerCount(m_layerCount));
		blitRegion.setDstOffsets(dstOffsets);

		cmd->blitImage(m_image, vk::ImageLayout::eTransferSrcOptimal, m_image, vk::ImageLayout::eTransferDstOptimal, blitRegion, vk::Filter::eLinear);

		//if (level < m_mipLevels - 1)
		//	TransitionLayout(cmd, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	}

	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();
}

thread_local std::shared_ptr<PassImageStateRecord> _record;

void VKWrapper::PassImageStateRecord::StartRecord()
{
	_record = std::make_shared<PassImageStateRecord>();
}

void VKWrapper::PassImageStateRecord::AddState(std::shared_ptr<const BaseVKImage> image, ImageLayout::BindStage stage, ImageLayout::BindUsage usage)
{
	if (!_record || !image)
		return;
	// 只读资产贴图不参与补差：它的布局永远是只读
	if (image->IsSampledImmutable())
		return;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;
	GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, image->m_format, stage, usage);
	const SubresourceState declared{ newLayout, ImageLayout::GetAccessMaskForLayout(newLayout, dstStageMask) };

	size_t mipCount = image->m_mipLevels;
	{
		LockGuard guard(image->m_imageState.lock);
		mipCount = std::max(mipCount, image->m_imageState.mips.size());
	}

	Entry entry;
	entry.first.assign(mipCount, declared);
	entry.current = entry.first;

	LockGuard guard(_record->_mutex);
	_record->_entries[image] = std::move(entry);
}

void VKWrapper::PassImageStateRecord::EndRecord(const std::shared_ptr<VKWrapper::VKCommandBuffer>& auxCmd)
{
	if (!_record)
		return;

	// 先摘掉线程局部的记录：下面写回真实记录时，不能再被当成「Pass 录制期间」
	auto record = std::move(_record);
	_record.reset();

	LockGuard recordGuard(record->_mutex);
	for (auto& [image, entry] : record->_entries) {
		if (!image)
			continue;

		LockGuard imageGuard(image->m_imageState.lock);
		auto& real = image->m_imageState.mips;
		if (!image->m_image)
			continue;
		if (real.size() < std::max(entry.first.size(), entry.current.size()))
			real.resize(std::max(entry.first.size(), entry.current.size()));

		// 补差：真实态 → 声明态（主 cmd 的起点）
		for (uint32_t level = 0; level < entry.first.size(); ++level)
		{
			const auto& realState = real[level];
			const auto& recordState = entry.first[level];
			if (realState.layout == recordState.layout && realState.accessMask == recordState.accessMask)
				continue;

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

		// 把本地终态写回真实态
		for (uint32_t level = 0; level < entry.current.size(); ++level)
			real[level] = entry.current[level];
	}
}

std::shared_ptr<PassImageStateRecord> VKWrapper::PassImageStateRecord::Current()
{
	return _record;
}

PassImageStateRecord::Entry& VKWrapper::PassImageStateRecord::GetEntryLocked(const std::shared_ptr<const BaseVKImage>& image)
{
	auto it = _entries.find(image);
	if (it != _entries.end())
		return it->second;

	Entry entry;
	{
		LockGuard guard(image->m_imageState.lock);
		entry.first = image->m_imageState.mips;
	}
	if (entry.first.size() < image->m_mipLevels)
		entry.first.resize(image->m_mipLevels);
	entry.current = entry.first;
	return _entries.emplace(image, std::move(entry)).first->second;
}
