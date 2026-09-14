#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/VKContext.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>   
#include "VulkanRenderEngine/General/DDSLoader.h"

static float GetAnisotropicTextureFiltering()
{
	static std::optional<float> s_value;
	static std::mutex s_mutex;

	if (s_value.has_value())
		return s_value.value();

	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_value.has_value())
		return s_value.value();

	if (auto device = VKCONTEXT->GetDevice())
		s_value = device->GetPhysicalDeviceProperties().properties.limits.maxSamplerAnisotropy;
	else
		return 1.0f;  // 返回默认值

	return s_value.value();
}

static vk::ImageAspectFlags GetAspectMaskForFormat(vk::Format format, bool prefeerdDepth = true) {
	switch (format) {
		// ===== 颜色格式 =====
		// 8-bit
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
		return vk::ImageAspectFlagBits::eColor;

		// ===== 深度格式 =====
	case vk::Format::eD16Unorm:
	case vk::Format::eX8D24UnormPack32:
	case vk::Format::eD32Sfloat:
		return vk::ImageAspectFlagBits::eDepth;

		// ===== 深度+模板组合格式 =====
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
		if (prefeerdDepth)
			return vk::ImageAspectFlagBits::eDepth;
		else
			return vk::ImageAspectFlagBits::eStencil;

		// ===== 纯模板格式 =====
	case vk::Format::eS8Uint:
		return vk::ImageAspectFlagBits::eStencil;

		// ===== 未知格式 =====
	default:
		// 默认返回 COLOR（颜色图像最常见）
		return vk::ImageAspectFlagBits::eColor;
	}
}

void Texture2D::GetImageLayoutAndStageFlag(vk::ImageLayout* outLayout, vk::PipelineStageFlags* outDestStageFlag, vk::Format format, BindStage stage, BindUsage usage)
{
	using Layout = vk::ImageLayout;
	using StageFlag = vk::PipelineStageFlagBits;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	bool isColorFormat = VKWrapper::VmaImage::IsColorFormat(format);
	bool isDepthStencilFormat = VKWrapper::VmaImage::IsDepthStencilFormat(format);
	bool isDepthFormat = VKWrapper::VmaImage::IsDepthFormat(format);
	bool isStencilFormat = VKWrapper::VmaImage::IsStencilFormat(format);

	if (usage == BindUsage::Sample)
	{
		if (stage == BindStage::Compute)
		{
			dstStageMask = StageFlag::eComputeShader;
			newLayout = Layout::eGeneral;
		}
		else if (stage == BindStage::Graphics)
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
		else if (stage == BindStage::RayTracing)
		{
			dstStageMask = StageFlag::eRayTracingShaderKHR;
			newLayout = Layout::eGeneral;
		}
	}
	else if (usage == BindUsage::Output)
	{
		if (stage == BindStage::Compute)
		{
			dstStageMask = StageFlag::eComputeShader;
			newLayout = Layout::eGeneral;
		}
		else if (stage == BindStage::Graphics)
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
		else if (stage == BindStage::RayTracing)
		{
			dstStageMask = StageFlag::eRayTracingShaderKHR;
			newLayout = Layout::eGeneral;
		}
	}

	if (outLayout)
		*outLayout = newLayout;
	if (outDestStageFlag)
		*outDestStageFlag = dstStageMask;
}

void Texture2D::BlitImage(Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();
	BlitImageAsync(cmd, src, dstImage, dstWidth, dstHeight);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

bool Texture2D::CopyTexture(Texture2D& src, Texture2D& dest, uint32_t srcLevel, uint32_t destLevel)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();
	bool result = CopyTextureAsync(cmd, src, dest, srcLevel, destLevel);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	return result;
}

bool Texture2D::CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel, uint32_t destLevel)
{
	if (!src || !dest)
		return false;
	return CopyTexture(*src, *dest, srcLevel, destLevel);
}

void Texture2D::BlitImageAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight)
{
	vk::Image srcImage = src._image->GetHandle();
	vk::ImageLayout srcLayout = src._image->GetCurrentLayout(0);

	// 切换源到 TRANSFER_SRC
	if (srcLayout != vk::ImageLayout::eTransferSrcOptimal) {
		src._image->TransitionLayout(
			cmd,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			0,
			1
		);
	}

	std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)src.GetWidth(), (int)src.GetHeight(), 1 } };
	std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)dstWidth, (int)dstHeight, 1 } };

	vk::ImageBlit blitRegion = {};
	blitRegion.setSrcSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1));
	blitRegion.setSrcOffsets(srcOffsets);
	blitRegion.setDstSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1));
	blitRegion.setDstOffsets(dstOffsets);

	cmd->blitImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, blitRegion, vk::Filter::eLinear);
}

bool Texture2D::CopyTextureAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest, uint32_t srcLevel, uint32_t destLevel)
{
	using namespace VKWrapper;

	if (&src == &dest && srcLevel == destLevel)
		return true;

	if (src.m_MaxLevel <= srcLevel || dest.m_MaxLevel <= destLevel)
		return false;

	if (src.m_Format != dest.m_Format)
		return false;

	if (!src._image || !dest._image)
		return false;

	uint32_t srcWidth = std::max(1u, src.m_Width >> srcLevel);
	uint32_t srcHeight = std::max(1u, src.m_Height >> srcLevel);

	uint32_t destWidth = std::max(1u, dest.m_Width >> destLevel);
	uint32_t destHeight = std::max(1u, dest.m_Height >> destLevel);

	if (srcWidth != destWidth || srcHeight != destHeight)
		return false;

	if (!cmd)
		return false;

	// 获取源和目标图像
	vk::Image srcImage = src._image->GetHandle();
	vk::Image dstImage = dest._image->GetHandle();

	// 保存当前布局（用于恢复）
	vk::ImageLayout srcOldLayout = src._image->GetCurrentLayout(srcLevel);
	vk::ImageLayout dstOldLayout = dest._image->GetCurrentLayout(destLevel);

	// 切换源到 TRANSFER_SRC
	if (srcOldLayout != vk::ImageLayout::eTransferSrcOptimal) {
		src._image->TransitionLayout(
			cmd,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			srcLevel,
			1
		);
	}

	// 切换目标到 TRANSFER_DST
	if (dstOldLayout != vk::ImageLayout::eTransferDstOptimal) {
		dest._image->TransitionLayout(
			cmd,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			destLevel,
			1
		);
	}

	vk::ImageSubresourceLayers subresourceLayers;
	subresourceLayers
		.setAspectMask(GetAspectMaskForFormat(src.m_Format))
		.setMipLevel(srcLevel)
		.setBaseArrayLayer(0)
		.setLayerCount(1);

	vk::ImageCopy region;
	region
		.setSrcSubresource(subresourceLayers)
		.setSrcOffset({ 0, 0, 0 })
		.setDstSubresource(
			vk::ImageSubresourceLayers()
			.setAspectMask(GetAspectMaskForFormat(dest.m_Format))
			.setMipLevel(destLevel)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
		)
		.setDstOffset({ 0, 0, 0 })
		.setExtent({ srcWidth, srcHeight, 1 });

	// 执行拷贝
	cmd->copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal,
		dstImage, vk::ImageLayout::eTransferDstOptimal,
		region);

	// 恢复源布局
	if (srcOldLayout != vk::ImageLayout::eTransferSrcOptimal && srcOldLayout != vk::ImageLayout::eUndefined) {
		src._image->TransitionLayout(
			cmd,
			srcOldLayout,
			VmaImage::AccessMaskToStage(VmaImage::GetAccessMaskForLayout(srcOldLayout))
		);
	}

	// 恢复目标布局
	if (dstOldLayout != vk::ImageLayout::eTransferDstOptimal && dstOldLayout != vk::ImageLayout::eUndefined) {
		dest._image->TransitionLayout(
			cmd,
			dstOldLayout,
			VmaImage::AccessMaskToStage(VmaImage::GetAccessMaskForLayout(dstOldLayout))
		);
	}

	return true;
}

bool Texture2D::CopyTextureAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel, uint32_t destLevel)
{
	if (!src || !dest)
		return false;
	return CopyTextureAsync(cmd, *src, *dest, srcLevel, destLevel);
}

Texture2D::Texture2D(const std::string& filepath, const Texture2DConfig& config)
	: m_Width(0), m_Height(0), m_MaxLevel(1), m_config(config)
{
	LoadFromFile(filepath);
}

Texture2D::Texture2D(uint32_t width, uint32_t height, vk::Format format, const Texture2DConfig& config, uint32_t level)
	: m_Width(width), m_Height(height), m_Format(format), m_MaxLevel(std::max(1u, level)), m_config(config)
{
	CreateImage();
	CreateImageView();
	CreateSampler();
}

Texture2D::Texture2D(std::shared_ptr<SharedTexture> sharedTexture, const Texture2DConfig& config)
	: m_Width(sharedTexture->width), m_Height(sharedTexture->height), m_MaxLevel(1u), m_config(config)
{
	CreateFromDX11SharedHandle(sharedTexture);
	CreateImageView();
	CreateSampler();
}


Texture2D::~Texture2D()
{
	BindlessTextureManager::Instance()->UnregisterTexture(this);
}

Texture2D& Texture2D::SetFiltering(vk::Filter xFilter)
{
	if (m_config.minFilter == xFilter && m_config.magFilter == xFilter)
		return *this;

	m_config.minFilter = xFilter;
	m_config.magFilter = xFilter;
	CreateSampler();
	return *this;
}

Texture2D& Texture2D::SetFiltering(vk::Filter minFilter, vk::Filter magFilter)
{
	if (m_config.minFilter == minFilter && m_config.magFilter == magFilter)
		return *this;

	m_config.minFilter = minFilter;
	m_config.magFilter = magFilter;
	CreateSampler();
	return *this;
}

Texture2D& Texture2D::SetWrapping(vk::SamplerAddressMode wrapX)
{
	if (m_config.wrapU == wrapX && m_config.wrapV == wrapX)
		return *this;

	m_config.wrapU = wrapX;
	m_config.wrapV = wrapX;
	CreateSampler();
	return *this;
}

Texture2D& Texture2D::SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV)
{
	if (m_config.wrapU == wrapU && m_config.wrapV == wrapV)
		return *this;

	m_config.wrapU = wrapU;
	m_config.wrapV = wrapV;
	CreateSampler();
	return *this;
}

Texture2D& Texture2D::SetAnisotropy(bool anisotropy)
{
	if (m_config.anisotropy == anisotropy)
		return *this;

	m_config.anisotropy = anisotropy;
	CreateSampler();
	return *this;
}

vk::Format Texture2D::GetFormat() const
{
	return m_Format;
}

vk::Filter Texture2D::GetMinFilter() const
{
	return m_config.minFilter;
}

vk::Filter Texture2D::GetMagFilter() const
{
	return m_config.magFilter;
}

vk::SamplerAddressMode Texture2D::GetWrapU() const
{
	return m_config.wrapU;
}

vk::SamplerAddressMode Texture2D::GetWrapV() const
{
	return m_config.wrapV;
}

void Texture2D::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout layout, vk::PipelineStageFlags dstAccessMask, uint32_t level)
{
	if (!cmd) return;

	if (level == UINT32_MAX)
	{
		_image->TransitionLayout(
			cmd,
			layout,
			dstAccessMask
		);
	}
	else
	{
		_image->TransitionLayout(
			cmd,
			layout,
			dstAccessMask,
			level,
			1
		);
	}
}

void Texture2D::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, BindStage stage, BindUsage usage, uint32_t level)
{
	if (!cmd) return;

	using Layout = vk::ImageLayout;
	using StageFlag = vk::PipelineStageFlagBits;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, m_Format, stage, usage);

	if (level == UINT32_MAX)
	{
		_image->TransitionLayout(
			cmd,
			newLayout,
			dstStageMask
		);
	}
	else
	{
		_image->TransitionLayout(
			cmd,
			newLayout,
			dstStageMask,
			level,
			1
		);
	}

	if (outLayout)
		*outLayout = newLayout;
}

void Texture2D::Barrier(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, BindStage stage, BindUsage usage, uint32_t level)
{
	if (!cmd) return;

	using Layout = vk::ImageLayout;
	using StageFlag = vk::PipelineStageFlagBits;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, m_Format, stage, usage);

	if (level == UINT32_MAX)
	{
		_image->TransitionLayout(
			cmd,
			newLayout,
			dstStageMask,
			0,
			vk::RemainingMipLevels,
			true
		);
	}
	else
	{
		_image->TransitionLayout(
			cmd,
			newLayout,
			dstStageMask,
			level,
			1,
			true
		);
	}

	if (outLayout)
		*outLayout = newLayout;
}

vk::Image Texture2D::GetImage() const
{
	return _image->GetHandle();
}

vk::ImageView Texture2D::GetImageView(uint32_t baseMipLevel, uint32_t levelCount) const
{
	if (baseMipLevel == 0 && levelCount == UINT32_MAX)
	{
		if (auto imageView = _imageView)
			return imageView->GetHandle();
		return VK_NULL_HANDLE;
	}
	else
	{
		LockGuard guard(_mutex);
		LevelInfo info(baseMipLevel, levelCount);
		if (auto it = _levelImageViews.find(info); it != _levelImageViews.end())
			return it->second->GetHandle();
		else
		{
			auto levelImageView = CreateLevelImageView(baseMipLevel, levelCount);
			if (!levelImageView)
				return VK_NULL_HANDLE;
			_levelImageViews[info] = levelImageView;
			return levelImageView->GetHandle();
		}
	}
}

vk::Sampler Texture2D::GetSampler() const
{
	if (auto sampler = _sampler)
		return sampler->GetHandle();
	return VK_NULL_HANDLE;
}

//GLuint64 Texture2D::GetBindlessID() const
//{
//	return t_ThreadResidentProxy.GetThreadBindlessData(this, _BindlessVersion.load());
//}

TextureDescBindEntry Texture2D::GetDescBindEntry(uint32_t baseMipLevel, uint32_t levelCount) const
{
	if (baseMipLevel == 0 && levelCount == UINT32_MAX)
	{
		LockGuard guard(_mutex);
		return TextureDescBindEntry{ .image = _image, .imageView = _imageView, .sampler = _sampler, .version = _version };
	}
	else
	{
		LockGuard guard(_mutex);
		LevelInfo info(baseMipLevel, levelCount);
		if (auto it = _levelImageViews.find(info); it != _levelImageViews.end())
			return TextureDescBindEntry{ .image = _image, .imageView = it->second, .sampler = _sampler, .version = _version };
		else
		{
			auto levelImageView = CreateLevelImageView(baseMipLevel, levelCount);
			if (!levelImageView)
				return TextureDescBindEntry{ .image = _image, .imageView = nullptr, .sampler = _sampler, .version = _version };
			_levelImageViews[info] = levelImageView;
			return TextureDescBindEntry{ .image = _image, .imageView = levelImageView, .sampler = _sampler, .version = _version };
		}
	}
}

uint32_t Texture2D::GetDescBindEntryVersion() const
{
	return _version;
}

uint32_t Texture2D::GetWidth() const
{
	return m_Width;
}

uint32_t Texture2D::GetHeight() const
{
	return m_Height;
}

uint32_t Texture2D::GetMaxLevel() const
{
	return m_MaxLevel;
}

glm::u32vec2 Texture2D::GetSize() const
{
	return glm::u32vec2(m_Width, m_Height);
}

Texture2DConfig Texture2D::GetConfig() const
{
	return m_config;
}

void Texture2D::UpdateTextureData(void* data, uint32_t level) {

	uint32_t maxLevelIdx = m_MaxLevel - 1;
	if (level > maxLevelIdx)
		return;

	_image->UploadData(data, level);
}

bool Texture2D::IsEmpty() const
{
	return !_image || _image->GetHandle() == VK_NULL_HANDLE;
}

bool Texture2D::LoadFromFile(const std::string& filepath)
{
	LockGuard guard(_mutex);

	auto LoadFromData = [&](unsigned char* data, uint32_t width, uint32_t height, vk::Format format, uint32_t dataComponents) ->bool
		{
			m_Width = width;
			m_Height = height;
			m_Format = format;
			m_MaxLevel = 1;

			if (!CreateImage())
				return false;
			if (!CreateImageView())
				return false;
			if (!CreateSampler())
				return false;
			vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(width * height * dataComponents);
			if (!_image->UploadData(data, 0))
				return false;

			return true;
		};

	if (Tool::ToLower(fs::path(filepath).extension().string()) == ".dds")
	{
		vk::Format format = m_config.gammaCorrection ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
		DDSLoadResult result = LoadDDSFile(filepath, format);
		if (!result.valid)
		{
			//std::cout << "Texture failed to load at path: " << filepath << std::endl;
			return false;
		}
		return LoadFromData(result.data.data(), result.width, result.height, format, 4);
	}
	else
	{
		int reqComponents = 4;
		int width, height, realComponents;
		unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &realComponents, reqComponents);
		if (!data)
		{
			// std::cout << "Texture failed to load at path: " << filepath << std::endl;
			return false;
		}

		int dataComponents = reqComponents > 0 ? reqComponents : realComponents;

		// 根据通道数确定格式
		vk::Format format;
		if (dataComponents == 1)
		{
			format = m_config.gammaCorrection ? vk::Format::eR8Srgb : vk::Format::eR8Unorm;
		}
		else if (dataComponents == 3)
		{
			format = m_config.gammaCorrection ? vk::Format::eR8G8B8Srgb : vk::Format::eR8G8B8Unorm;
		}
		else if (dataComponents == 4)
		{
			format = m_config.gammaCorrection ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
		}
		else
		{
			stbi_image_free(data);
			return false;
		}

		bool result = LoadFromData(data, width, height, format, dataComponents);
		stbi_image_free(data);
		return result;
	}
}

void Texture2D::Resize(uint32_t width, uint32_t height)
{
	if (width == m_Width && height == m_Height)
		return;
	m_Width = width;
	m_Height = height;
	CreateImage();
	CreateImageView();
}

bool Texture2D::CreateImage()
{
	LockGuard guard(_mutex);

	auto image = std::make_shared<VKWrapper::VmaImage>();
	bool result = image->Create(VKCONTEXT->GetDevice().get(), m_Format, { m_Width ,m_Height }, m_MaxLevel);
	if (!result)
		return false;

	_image = image;
	_version++;

	return true;
}

bool Texture2D::CreateImageView()
{
	LockGuard guard(_mutex);

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(GetAspectMaskForFormat(m_Format))
			.setBaseMipLevel(0)
			.setLevelCount(vk::RemainingMipLevels)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
		);

	auto imageView = std::make_shared<VKWrapper::VKImageView>();
	vk::Result result = imageView->Create(VKCONTEXT->GetDevice().get(), viewInfo);
	if (result != vk::Result::eSuccess)
		return false;

	_imageView = imageView;
	_levelImageViews.clear();
	_version++;

	return true;
}

bool Texture2D::CreateSampler()
{
	LockGuard guard(_mutex);

	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo
		.setMinFilter(m_config.minFilter)
		.setMagFilter(m_config.magFilter)
		.setAddressModeU(m_config.wrapU)
		.setAddressModeV(m_config.wrapV)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMipLodBias(0.f)
		.setAnisotropyEnable(m_config.anisotropy)
		.setMinLod(0)
		.setMaxLod(FLT_MAX)
		.setBorderColor(vk::BorderColor::eFloatOpaqueBlack)
		.setUnnormalizedCoordinates(vk::False);

	if (m_config.anisotropy)
		samplerInfo.setMaxAnisotropy(GetAnisotropicTextureFiltering());

	auto sampler = std::make_shared<VKWrapper::VKSampler>();
	vk::Result result = sampler->Create(VKCONTEXT->GetDevice().get(), samplerInfo);
	if (result != vk::Result::eSuccess)
		return false;

	_sampler = sampler;
	_version++;

	return true;
}

std::shared_ptr<VKWrapper::VKImageView> Texture2D::CreateLevelImageView(uint32_t BaseMipLevel, uint32_t LevelCount) const
{

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(GetAspectMaskForFormat(m_Format))
			.setBaseMipLevel(BaseMipLevel)
			.setLevelCount(LevelCount)
			.setBaseArrayLayer(0)
			.setLayerCount(1)
		);

	auto imageView = std::make_shared<VKWrapper::VKImageView>();
	vk::Result result = imageView->Create(VKCONTEXT->GetDevice().get(), viewInfo);
	if (result != vk::Result::eSuccess)
		return nullptr;

	return imageView;
}

bool Texture2D::CreateFromDX11SharedHandle(std::shared_ptr<SharedTexture> sharedTexture)
{
	LockGuard guard(_mutex);

	auto image = std::make_shared<VKWrapper::SharedImage>();
	bool result = image->Create(VKCONTEXT->GetDevice().get(), sharedTexture, m_Format);
	if (!result)
		return false;

	_image = image;
	_version++;

	return true;
}



bool Texture2DConfig::operator==(const Texture2DConfig& other)
{
	return minFilter == other.minFilter
		&& magFilter == other.magFilter
		&& wrapU == other.wrapU
		&& wrapV == other.wrapV
		&& anisotropy == other.anisotropy
		&& gammaCorrection == other.gammaCorrection;
}

bool Texture2DConfig::operator!=(const Texture2DConfig& other)
{
	return !(*this == other);
}
