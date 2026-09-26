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

void Texture2D::BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight)
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

void Texture2D::BlitImage(Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();
	BlitImageAsync(cmd, src, dstImage, dstWidth, dstHeight);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

bool Texture2D::BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest)
{
	vk::Image srcImage = src._image->GetHandle();
	vk::Image dstImage = dest._image->GetHandle();
	vk::ImageLayout srcLayout = src._image->GetCurrentLayout(0);
	vk::ImageLayout dstLayout = dest._image->GetCurrentLayout(0);

	if (srcLayout != vk::ImageLayout::eTransferSrcOptimal) {
		src._image->TransitionLayout(
			cmd,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			0,
			1
		);
	}

	if (dstLayout != vk::ImageLayout::eTransferDstOptimal) {
		dest._image->TransitionLayout(
			cmd,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			0,
			1
		);
	}

	std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)src.GetWidth(), (int)src.GetHeight(), 1 } };
	std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{ 0, 0, 0 },vk::Offset3D{ (int)dest.GetWidth(), (int)dest.GetHeight(), 1} };

	vk::ImageBlit blitRegion = {};
	blitRegion.setSrcSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1));
	blitRegion.setSrcOffsets(srcOffsets);
	blitRegion.setDstSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1));
	blitRegion.setDstOffsets(dstOffsets);

	cmd->blitImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, blitRegion, vk::Filter::eNearest);
	return true;
}

bool Texture2D::BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest) {
	if (!src || !dest)
		return false;
	return BlitImageAsync(cmd, *src, *dest);
}

bool Texture2D::BlitImage(Texture2D& src, Texture2D& dest) {
	auto cmd = VKCONTEXT->GetCommandBuffer();
	bool result = BlitImageAsync(cmd, src, dest);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	return result;
}

bool Texture2D::BlitImage(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest) {
	if (!src || !dest)
		return false;
	return BlitImage(*src, *dest);
}

bool Texture2D::CopyTextureAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest, uint32_t srcLevel, uint32_t destLevel)
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

	vk::ImageLayout srcOldLayout = src._image->GetCurrentLayout(srcLevel);
	vk::ImageLayout dstOldLayout = dest._image->GetCurrentLayout(destLevel);

	auto srcAspectMask = ImageLayout::GetAspectMaskForFormat(src.m_Format);
	auto dstAspectMask = ImageLayout::GetAspectMaskForFormat(dest.m_Format);

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

	vk::ImageSubresourceLayers srcSubresourceLayers;
	srcSubresourceLayers
		.setAspectMask(srcAspectMask)
		.setMipLevel(srcLevel)
		.setBaseArrayLayer(0)
		.setLayerCount(1);

	vk::ImageSubresourceLayers dstSubresourceLayers;
	dstSubresourceLayers
		.setAspectMask(dstAspectMask)
		.setMipLevel(destLevel)
		.setBaseArrayLayer(0)
		.setLayerCount(1);

	vk::ImageCopy region;
	region
		.setSrcSubresource(srcSubresourceLayers)
		.setSrcOffset({ 0, 0, 0 })
		.setDstSubresource(dstSubresourceLayers)
		.setDstOffset({ 0, 0, 0 })
		.setExtent({ srcWidth, srcHeight, 1 });

	// 执行拷贝
	cmd->copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal,
		dstImage, vk::ImageLayout::eTransferDstOptimal,
		region);

	return true;
}

bool Texture2D::CopyTextureAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel, uint32_t destLevel)
{
	if (!src || !dest)
		return false;
	return CopyTextureAsync(cmd, *src, *dest, srcLevel, destLevel);
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

Texture2D::Texture2D(const std::string& filepath, const Texture2DConfig& config, bool autoMipMaps)
	: m_Width(0), m_Height(0), m_MaxLevel(1u), m_config(config)
{
	LoadFromFile(filepath, autoMipMaps);
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

void Texture2D::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
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

void Texture2D::Barrier(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
{
	if (!cmd) return;

	using Layout = vk::ImageLayout;
	using StageFlag = vk::PipelineStageFlagBits;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	ImageLayout::GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, m_Format, stage, usage);

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

vk::ImageView Texture2D::GetImageView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetImageView(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

vk::ImageView Texture2D::GetImageView(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);
	if (auto it = _levelImageViews.find(info); it != _levelImageViews.end())
		return it->second->GetHandle();
	else
	{
		auto levelImageView = CreateLevelImageView(info);
		if (!levelImageView)
			return VK_NULL_HANDLE;
		_levelImageViews[info] = levelImageView;
		return levelImageView->GetHandle();
	}
}

vk::Sampler Texture2D::GetSampler() const
{
	if (auto sampler = _sampler)
		return sampler->GetHandle();
	return VK_NULL_HANDLE;
}

TextureDescBindEntry Texture2D::GetDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetDescBindEntry(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

TextureDescBindEntry Texture2D::GetDescBindEntry(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);
	if (auto it = _levelImageViews.find(info); it != _levelImageViews.end())
		return TextureDescBindEntry{ .image = _image, .imageView = it->second, .sampler = _sampler, .version = _version };
	else
	{
		auto levelImageView = CreateLevelImageView(info);
		if (!levelImageView)
			return TextureDescBindEntry{ .image = _image, .imageView = nullptr, .sampler = _sampler, .version = _version };
		_levelImageViews[info] = levelImageView;
		return TextureDescBindEntry{ .image = _image, .imageView = levelImageView, .sampler = _sampler, .version = _version };
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

void Texture2D::GenerateTextureMipMaps()
{
	if (m_MaxLevel <= 1)
		return;

	if (auto img = _image)
		img->GenerateMipMaps();
}

std::shared_ptr<VKWrapper::BaseVKImage> Texture2D::GetImageSharedPtr() const { return _image; }

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

bool Texture2D::LoadFromFile(const std::string& filepath, bool autoMipMaps)
{
	LockGuard guard(_mutex);

	auto LoadFromData = [&](unsigned char* data, uint32_t width, uint32_t height, vk::Format format, uint32_t dataComponents) ->bool
		{
			m_Width = width;
			m_Height = height;
			m_Format = format;
			m_MaxLevel = autoMipMaps ? floor(log2(std::max(width, height))) + 1 : 1u;

			if (autoMipMaps)
				m_MaxLevel = std::min(m_MaxLevel, 7u);

			if (!CreateImage())
				return false;
			if (!CreateImageView())
				return false;
			if (!CreateSampler())
				return false;
			vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(width * height * dataComponents);
			if (!_image->UploadData(data, 0))
				return false;

			if (m_MaxLevel > 1)
				GenerateTextureMipMaps();

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
	_levelImageViews.clear();
	_version++;
	return true;
}

bool Texture2D::CreateSampler()
{
	m_config.anisotropy = false;
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

std::shared_ptr<VKWrapper::VKImageView> Texture2D::CreateLevelImageView(const ImageViewInfo& info) const
{

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(info.aspect)
			.setBaseMipLevel(info.base)
			.setLevelCount(info.count)
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
