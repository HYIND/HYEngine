#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/BaseVKImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

bool BaseVKImage::IsColorFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
	case vk::Format::eS8Uint:
		return false;
	default:
		return true;
	}
}

bool BaseVKImage::IsDepthStencilFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
		return true;
	default:
		return false;
	}
}

bool BaseVKImage::IsDepthFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
		return true;
	default:
		return false;
	}
}

bool BaseVKImage::IsStencilFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eS8Uint:
		return true;
	default:
		return false;
	}
}

bool BaseVKImage::IsLinearFormat(vk::Format format)
{
	switch (format)
	{
		// 所有 sRGB 格式都是非线性的
	case vk::Format::eR8Srgb:
	case vk::Format::eR8G8Srgb:
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8Srgb:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eA8B8G8R8SrgbPack32:
		// BC 压缩格式的 sRGB 变体
	case vk::Format::eBc1RgbSrgbBlock:
	case vk::Format::eBc1RgbaSrgbBlock:
	case vk::Format::eBc2SrgbBlock:
	case vk::Format::eBc3SrgbBlock:
	case vk::Format::eBc7SrgbBlock:
		// ETC 压缩格式的 sRGB 变体
	case vk::Format::eEtc2R8G8B8SrgbBlock:
	case vk::Format::eEtc2R8G8B8A1SrgbBlock:
	case vk::Format::eEtc2R8G8B8A8SrgbBlock:
		// ASTC 压缩格式的 sRGB 变体
	case vk::Format::eAstc4x4SrgbBlock:
	case vk::Format::eAstc5x4SrgbBlock:
	case vk::Format::eAstc5x5SrgbBlock:
	case vk::Format::eAstc6x5SrgbBlock:
	case vk::Format::eAstc6x6SrgbBlock:
	case vk::Format::eAstc8x5SrgbBlock:
	case vk::Format::eAstc8x6SrgbBlock:
	case vk::Format::eAstc8x8SrgbBlock:
	case vk::Format::eAstc10x5SrgbBlock:
	case vk::Format::eAstc10x6SrgbBlock:
	case vk::Format::eAstc10x8SrgbBlock:
	case vk::Format::eAstc10x10SrgbBlock:
	case vk::Format::eAstc12x10SrgbBlock:
	case vk::Format::eAstc12x12SrgbBlock:
		return false;
	default:
		return true;  // 默认认为是线性格式
	}
}

vk::AccessFlags BaseVKImage::GetAccessMaskForLayout(vk::ImageLayout layout) {
	switch (layout) {
	case vk::ImageLayout::eUndefined:
		return vk::AccessFlagBits::eNone;

	case vk::ImageLayout::eGeneral:
		return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

	case vk::ImageLayout::eColorAttachmentOptimal:
		return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eDepthAttachmentOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eStencilAttachmentOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead;

	case vk::ImageLayout::eShaderReadOnlyOptimal:
		return vk::AccessFlagBits::eShaderRead;

	case vk::ImageLayout::eTransferSrcOptimal:
		return vk::AccessFlagBits::eTransferRead;

	case vk::ImageLayout::eTransferDstOptimal:
		return vk::AccessFlagBits::eTransferWrite;

	case vk::ImageLayout::ePreinitialized:
		return vk::AccessFlagBits::eHostWrite;

	case vk::ImageLayout::ePresentSrcKHR:
		return vk::AccessFlagBits::eNone;  // 显示引擎不需要 AccessMask

	case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eDepthReadOnlyOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	case vk::ImageLayout::eStencilReadOnlyOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	default:
		return vk::AccessFlagBits::eNone;
	}
}

// srcStageMask：从当前 AccessMask 推导
vk::PipelineStageFlags BaseVKImage::AccessMaskToStage(vk::AccessFlags mask) {
	if (!mask) {
		return vk::PipelineStageFlagBits::eTopOfPipe;
	}

	// 注意：按优先级从高到低判断，因为一个 mask 可能包含多个 bit
	// 如果同时包含多个，取"最保守"的那个

	// 主机操作
	if (mask & (vk::AccessFlagBits::eHostRead | vk::AccessFlagBits::eHostWrite)) {
		return vk::PipelineStageFlagBits::eHost;
	}

	// 传输操作
	if (mask & (vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite)) {
		return vk::PipelineStageFlagBits::eTransfer;
	}

	// 颜色附件输出
	if (mask & (vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite)) {
		return vk::PipelineStageFlagBits::eColorAttachmentOutput;
	}

	// 深度模板附件
	if (mask & (vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite)) {
		return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
	}

	// 着色器写入（通常是 Compute）
	if (mask & vk::AccessFlagBits::eShaderWrite) {
		return vk::PipelineStageFlagBits::eComputeShader;
	}

	// 着色器读取（保守：等待所有图形阶段）
	if (mask & vk::AccessFlagBits::eShaderRead) {
		return vk::PipelineStageFlagBits::eAllGraphics;
	}

	// 索引/顶点/Uniform 读取（都属于顶点输入阶段）
	if (mask & (vk::AccessFlagBits::eIndexRead | vk::AccessFlagBits::eVertexAttributeRead)) {
		return vk::PipelineStageFlagBits::eVertexInput;
	}

	if (mask & vk::AccessFlagBits::eUniformRead) {
		return vk::PipelineStageFlagBits::eAllGraphics;
	}

	// 间接命令读取
	if (mask & vk::AccessFlagBits::eIndirectCommandRead) {
		return vk::PipelineStageFlagBits::eDrawIndirect;
	}

	return vk::PipelineStageFlagBits::eTopOfPipe;
}

static size_t GetFormatSize(vk::Format format) {
	switch (format) {
		// 8-bit 单通道
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Snorm:
	case vk::Format::eR8Uint:
	case vk::Format::eR8Sint:
		return 1;

		// 8-bit 双通道
	case vk::Format::eR8G8Unorm:
	case vk::Format::eR8G8Snorm:
	case vk::Format::eR8G8Uint:
	case vk::Format::eR8G8Sint:
		return 2;

		// 8-bit 三通道（24 字节，但 GPU 通常对齐到 4）
	case vk::Format::eR8G8B8Unorm:
	case vk::Format::eR8G8B8Snorm:
	case vk::Format::eR8G8B8Uint:
	case vk::Format::eR8G8B8Sint:
		return 3;

		// 8-bit 四通道（最常用）
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eB8G8R8A8Unorm:
		return 4;

		// 16-bit 单通道
	case vk::Format::eR16Unorm:
	case vk::Format::eR16Snorm:
	case vk::Format::eR16Uint:
	case vk::Format::eR16Sint:
	case vk::Format::eR16Sfloat:
		return 2;

		// 16-bit 双通道
	case vk::Format::eR16G16Unorm:
	case vk::Format::eR16G16Snorm:
	case vk::Format::eR16G16Uint:
	case vk::Format::eR16G16Sint:
	case vk::Format::eR16G16Sfloat:
		return 4;

		// 16-bit 四通道
	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	case vk::Format::eR16G16B16A16Uint:
	case vk::Format::eR16G16B16A16Sint:
	case vk::Format::eR16G16B16A16Sfloat:
		return 8;

		// 32-bit 单通道
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
		return 4;

		// 32-bit 双通道
	case vk::Format::eR32G32Uint:
	case vk::Format::eR32G32Sint:
	case vk::Format::eR32G32Sfloat:
		return 8;

		// 32-bit 四通道
	case vk::Format::eR32G32B32A32Uint:
	case vk::Format::eR32G32B32A32Sint:
	case vk::Format::eR32G32B32A32Sfloat:
		return 16;

		// 压缩格式（暂不处理）
	default:
		return 4;  // 默认按 RGBA8 处理
	}
}

vk::ImageAspectFlags BaseVKImage::GetAspectMask(vk::Format format)
{
	if (BaseVKImage::IsColorFormat(format))
		return vk::ImageAspectFlagBits::eColor;
	if (BaseVKImage::IsDepthStencilFormat(format))
		return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
	if (BaseVKImage::IsDepthFormat(format))
		return vk::ImageAspectFlagBits::eDepth;
	if (BaseVKImage::IsStencilFormat(format))
		return vk::ImageAspectFlagBits::eStencil;
	return vk::ImageAspectFlagBits::eColor;
};

vk::Image BaseVKImage::GetHandle() const { return m_image; }

vk::ImageLayout BaseVKImage::GetCurrentLayout(uint32_t level) const { return GetState(level).layout; }

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

	vk::AccessFlags newAccessMask = GetAccessMaskForLayout(newLayout);

	for (uint32_t level = baseMipLevel; level < std::min(m_mipLevels, baseMipLevel + levelCount); ++level)
	{
		auto& state = GetState(level);

		if (!force && state.layout == newLayout && state.accessMask == newAccessMask)
			continue;

		vk::PipelineStageFlags srcStageMask = AccessMaskToStage(state.accessMask);

		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.setAspectMask(m_aspectMask);
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

		cmd->pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, {}, {}, barrier);

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
	size_t bytesPerPixel = GetFormatSize(m_format);
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
	subresourceLayers.setAspectMask(m_aspectMask);
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

bool BaseVKImage::GenerateMipmaps(
	std::shared_ptr<VKCommandBuffer> cmd,
	vk::ImageLayout finalLayout,
	vk::PipelineStageFlags dstStageMask
) {
	if (!cmd || !m_image || m_mipLevels <= 1) {
		return false;
	}

	// 检查格式是否支持线性过滤（生成 Mipmap 需要）
	vk::FormatProperties formatProps = m_device->GetPhysicalDevice().getFormatProperties(m_format);

	if (!(formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
		// 不支持自动生成，返回失败（上层可以降级处理）
		return false;
	}

	//  确保整图在 TRANSFER_DST_OPTIMAL（方便后续操作）
	//  注意：当前 m_currentLayout 可能只是第 0 级的状态，但 GenerateMipmaps 需要操作所有级别
	//  所以需要先把整图切到 TRANSFER_DST_OPTIMAL
	TransitionLayout(
		cmd,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTransfer
	);

	// 逐级生成 Mipmap
	int32_t mipWidth = m_extent.width;
	int32_t mipHeight = m_extent.height;

	for (uint32_t i = 1; i < m_mipLevels; i++) {
		// ---- 将上一级（i-1）从 TRANSFER_DST 切换到 TRANSFER_SRC ----
		// 因为上一级刚被写入，需要切换到 SRC 才能作为 Blit 的源
		vk::ImageMemoryBarrier srcBarrier;
		srcBarrier
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)   // 上一步是写入
			.setDstAccessMask(vk::AccessFlagBits::eTransferRead)    // 下一步要读取
			.setImage(m_image)
			.setSubresourceRange(
				vk::ImageSubresourceRange()
				.setAspectMask(m_aspectMask)
				.setBaseMipLevel(i - 1)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			);

		cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, srcBarrier);

		// ---- 计算当前 Mip 的尺寸 ----
		mipWidth = std::max(1, mipWidth / 2);
		mipHeight = std::max(1, mipHeight / 2);

		// ---- 执行 Blit（缩放拷贝） ----
		vk::ImageBlit blit;

		std::array<vk::Offset3D, 2> srcOffsets = {
			vk::Offset3D(0, 0, 0),
			vk::Offset3D(mipWidth * 2, mipHeight * 2, 1)  // 上一级尺寸
		};

		std::array<vk::Offset3D, 2> dstOffsets = {
			vk::Offset3D(0, 0, 0),
			vk::Offset3D(mipWidth, mipHeight, 1)  // 当前级尺寸
		};

		blit.setSrcOffsets(srcOffsets)
			.setSrcSubresource(
				vk::ImageSubresourceLayers()
				.setAspectMask(m_aspectMask)
				.setMipLevel(i - 1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			)
			.setDstOffsets(dstOffsets)
			.setDstSubresource(
				vk::ImageSubresourceLayers()
				.setAspectMask(m_aspectMask)
				.setMipLevel(i)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			);

		cmd->blitImage(m_image, vk::ImageLayout::eTransferSrcOptimal, m_image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

		// ---- 将当前级从 TRANSFER_DST 切换到 TRANSFER_SRC ----
		// 准备作为下一轮 Blit 的源（只有当前级不是最后一级时才需要）
		if (i < m_mipLevels - 1) {
			vk::ImageMemoryBarrier dstBarrier;
			dstBarrier
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)   // 刚刚 Blit 写入
				.setDstAccessMask(vk::AccessFlagBits::eTransferRead)    // 下一轮要读取
				.setImage(m_image)
				.setSubresourceRange(
					vk::ImageSubresourceRange()
					.setAspectMask(m_aspectMask)
					.setBaseMipLevel(i)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				);

			cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, dstBarrier);
		}
	}

	// 将第 0 级从 TRANSFER_SRC 切回 TRANSFER_DST（因为第 0 级可能被改成了 SRC）
	// 注意：只有 m_mipLevels > 1 时才需要
	if (m_mipLevels > 1) {
		vk::ImageMemoryBarrier finalBarrier0;
		finalBarrier0
			.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
			.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
			.setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
			.setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setImage(m_image)
			.setSubresourceRange(
				vk::ImageSubresourceRange()
				.setAspectMask(m_aspectMask)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			);

		cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, finalBarrier0);
	}

	return true;
}