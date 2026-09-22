#include "vkstdafx.h"
#include "VulkanRenderEngine/General/ImageLayoutWrapper.h"

using namespace ImageLayout;

bool ImageLayout::IsColorFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Snorm:
	case vk::Format::eR8Uint:
	case vk::Format::eR8Sint:
	case vk::Format::eR8G8Unorm:
	case vk::Format::eR8G8Snorm:
	case vk::Format::eR8G8Uint:
	case vk::Format::eR8G8Sint:
	case vk::Format::eR8G8B8Unorm:
	case vk::Format::eR8G8B8Snorm:
	case vk::Format::eR8G8B8Uint:
	case vk::Format::eR8G8B8Sint:
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Snorm:
	case vk::Format::eR8Srgb:
	case vk::Format::eR8G8Srgb:
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Srgb:

		// 16-bit
	case vk::Format::eR16Unorm:
	case vk::Format::eR16Snorm:
	case vk::Format::eR16Uint:
	case vk::Format::eR16Sint:
	case vk::Format::eR16Sfloat:
	case vk::Format::eR16G16Unorm:
	case vk::Format::eR16G16Snorm:
	case vk::Format::eR16G16Uint:
	case vk::Format::eR16G16Sint:
	case vk::Format::eR16G16Sfloat:
	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	case vk::Format::eR16G16B16A16Uint:
	case vk::Format::eR16G16B16A16Sint:
	case vk::Format::eR16G16B16A16Sfloat:

		// 32-bit
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
	case vk::Format::eR32G32Uint:
	case vk::Format::eR32G32Sint:
	case vk::Format::eR32G32Sfloat:
	case vk::Format::eR32G32B32A32Uint:
	case vk::Format::eR32G32B32A32Sint:
	case vk::Format::eR32G32B32A32Sfloat:

		// 压缩格式（BC/ETC/ASTC 等）
	case vk::Format::eBc1RgbUnormBlock:
	case vk::Format::eBc1RgbSrgbBlock:
	case vk::Format::eBc1RgbaUnormBlock:
	case vk::Format::eBc1RgbaSrgbBlock:
	case vk::Format::eBc2UnormBlock:
	case vk::Format::eBc2SrgbBlock:
	case vk::Format::eBc3UnormBlock:
	case vk::Format::eBc3SrgbBlock:
	case vk::Format::eBc4UnormBlock:
	case vk::Format::eBc4SnormBlock:
	case vk::Format::eBc5UnormBlock:
	case vk::Format::eBc5SnormBlock:
	case vk::Format::eBc6HUfloatBlock:
	case vk::Format::eBc6HSfloatBlock:
	case vk::Format::eBc7UnormBlock:
	case vk::Format::eBc7SrgbBlock:
	case vk::Format::eEtc2R8G8B8UnormBlock:
	case vk::Format::eEtc2R8G8B8SrgbBlock:
	case vk::Format::eEtc2R8G8B8A1UnormBlock:
	case vk::Format::eEtc2R8G8B8A1SrgbBlock:
	case vk::Format::eEtc2R8G8B8A8UnormBlock:
	case vk::Format::eEtc2R8G8B8A8SrgbBlock:
	case vk::Format::eAstc4x4UnormBlock:
	case vk::Format::eAstc4x4SrgbBlock:
	case vk::Format::eAstc5x4UnormBlock:
	case vk::Format::eAstc5x4SrgbBlock:
	case vk::Format::eAstc5x5UnormBlock:
	case vk::Format::eAstc5x5SrgbBlock:
	case vk::Format::eAstc6x5UnormBlock:
	case vk::Format::eAstc6x5SrgbBlock:
	case vk::Format::eAstc6x6UnormBlock:
	case vk::Format::eAstc6x6SrgbBlock:
	case vk::Format::eAstc8x5UnormBlock:
	case vk::Format::eAstc8x5SrgbBlock:
	case vk::Format::eAstc8x6UnormBlock:
	case vk::Format::eAstc8x6SrgbBlock:
	case vk::Format::eAstc8x8UnormBlock:
	case vk::Format::eAstc8x8SrgbBlock:
	case vk::Format::eAstc10x5UnormBlock:
	case vk::Format::eAstc10x5SrgbBlock:
	case vk::Format::eAstc10x6UnormBlock:
	case vk::Format::eAstc10x6SrgbBlock:
	case vk::Format::eAstc10x8UnormBlock:
	case vk::Format::eAstc10x8SrgbBlock:
	case vk::Format::eAstc10x10UnormBlock:
	case vk::Format::eAstc10x10SrgbBlock:
	case vk::Format::eAstc12x10UnormBlock:
	case vk::Format::eAstc12x10SrgbBlock:
	case vk::Format::eAstc12x12UnormBlock:
	case vk::Format::eAstc12x12SrgbBlock:

		// 其他颜色格式
	case vk::Format::eR4G4UnormPack8:
	case vk::Format::eR4G4B4A4UnormPack16:
	case vk::Format::eB4G4R4A4UnormPack16:
	case vk::Format::eR5G6B5UnormPack16:
	case vk::Format::eB5G6R5UnormPack16:
	case vk::Format::eR5G5B5A1UnormPack16:
	case vk::Format::eB5G5R5A1UnormPack16:
	case vk::Format::eA1R5G5B5UnormPack16:
	case vk::Format::eA8B8G8R8UnormPack32:
	case vk::Format::eA8B8G8R8SnormPack32:
	case vk::Format::eA8B8G8R8SrgbPack32:
	case vk::Format::eA8B8G8R8UintPack32:
	case vk::Format::eA8B8G8R8SintPack32:
	case vk::Format::eA2R10G10B10UnormPack32:
	case vk::Format::eA2R10G10B10UintPack32:
	case vk::Format::eA2B10G10R10UnormPack32:
	case vk::Format::eA2B10G10R10UintPack32:
	case vk::Format::eG8B8G8R8422Unorm:
	case vk::Format::eB8G8R8G8422Unorm:
		return true;
	}
	return false;
}

bool ImageLayout::IsDepthStencilFormat(vk::Format format)
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

bool ImageLayout::IsDepthFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eD16Unorm:
	case vk::Format::eX8D24UnormPack32:
	case vk::Format::eD32Sfloat:
		return true;
	default:
		return false;
	}
}

bool ImageLayout::IsStencilFormat(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eS8Uint:
		return true;
	default:
		return false;
	}
}

bool ImageLayout::IsLinearFormat(vk::Format format)
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

vk::AccessFlags ImageLayout::GetAccessMaskForLayout(vk::ImageLayout layout, vk::PipelineStageFlags dstStageMask) {
	switch (layout) {
	case vk::ImageLayout::eUndefined:
		return vk::AccessFlagBits::eNone;

	case vk::ImageLayout::eGeneral:
		if (dstStageMask & vk::PipelineStageFlagBits::eTransfer)
			return vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
		if (dstStageMask &
			(vk::PipelineStageFlagBits::eComputeShader
				| vk::PipelineStageFlagBits::eVertexShader
				| vk::PipelineStageFlagBits::eFragmentShader
				| vk::PipelineStageFlagBits::eAllGraphics)
			)
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
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
		return vk::AccessFlagBits::eNone;

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
vk::PipelineStageFlags ImageLayout::AccessMaskToStage(vk::AccessFlags mask) {
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

uint32_t ImageLayout::GetFormatSize(vk::Format format) {
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

vk::ImageAspectFlags ImageLayout::GetAspectMaskForFormat(vk::Format format)
{
	if (ImageLayout::IsColorFormat(format))
		return vk::ImageAspectFlagBits::eColor;
	if (ImageLayout::IsDepthFormat(format))
		return vk::ImageAspectFlagBits::eDepth;
	if (ImageLayout::IsStencilFormat(format))
		return vk::ImageAspectFlagBits::eStencil;
	if (ImageLayout::IsDepthStencilFormat(format))
		return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
	return vk::ImageAspectFlagBits::eColor;
}

void ImageLayout::GetImageLayoutAndStageFlag(vk::ImageLayout* outLayout, vk::PipelineStageFlags* outDestStageFlag, vk::Format format, BindStage stage, BindUsage usage)
{
	using Layout = vk::ImageLayout;
	using StageFlag = vk::PipelineStageFlagBits;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	bool isColorFormat = ImageLayout::IsColorFormat(format);
	bool isDepthStencilFormat = ImageLayout::IsDepthStencilFormat(format);
	bool isDepthFormat = ImageLayout::IsDepthFormat(format);
	bool isStencilFormat = ImageLayout::IsStencilFormat(format);

	if (stage == BindStage::Compute)
	{
		dstStageMask = StageFlag::eComputeShader;
		newLayout = Layout::eGeneral;
	}
	else if (stage == BindStage::RayTracing)
	{
		dstStageMask = StageFlag::eRayTracingShaderKHR;
		newLayout = Layout::eGeneral;
	}
	else if (stage == BindStage::Graphics)
	{
		if (usage == BindUsage::Read)
		{
			dstStageMask = StageFlag::eVertexShader | StageFlag::eFragmentShader;
			if (isColorFormat)
				newLayout = Layout::eShaderReadOnlyOptimal;
			if (isDepthStencilFormat)
				newLayout = Layout::eDepthStencilReadOnlyOptimal;
			if (isDepthFormat)
				newLayout = Layout::eDepthReadOnlyOptimal;
			if (isStencilFormat)
				newLayout = Layout::eStencilReadOnlyOptimal;
		}
		else if (usage == BindUsage::Write)
		{
			if (isColorFormat)
			{
				dstStageMask = StageFlag::eColorAttachmentOutput;
				newLayout = Layout::eColorAttachmentOptimal;
			}
			else
			{
				dstStageMask = StageFlag::eEarlyFragmentTests;
				if (isDepthStencilFormat)
					newLayout = Layout::eDepthStencilAttachmentOptimal;
				if (isDepthFormat)
					newLayout = Layout::eDepthAttachmentOptimal;
				if (isStencilFormat)
					newLayout = Layout::eStencilAttachmentOptimal;
			}
		}
	}
	else if (stage == BindStage::Transfer)
	{
		dstStageMask = StageFlag::eTransfer;
		if (usage == BindUsage::Read) 
		{
			newLayout = Layout::eTransferSrcOptimal;
		}
		else if (usage == BindUsage::Write)
		{
			newLayout = Layout::eTransferDstOptimal;
		}
	}

	if (outLayout)
		*outLayout = newLayout;
	if (outDestStageFlag)
		*outDestStageFlag = dstStageMask;
}
