#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Texture3D.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/General/DDSLoader.h"

Texture3D::Texture3D(uint32_t width, uint32_t height, uint32_t depth, vk::Format format, const TextureConfig& config, uint32_t maxLevel)
	: m_Width(width), m_Height(height), m_Depth(depth), m_Format(format), m_MaxLevel(std::max(1u, maxLevel)), m_config(config)
{
	CreateImage();
	CreateImageView();
	CreateSampler();
}

Texture3D::Texture3D(const std::string& filepath, vk::Format format, const TextureConfig& config)
	: m_Width(1), m_Height(1), m_Depth(1), m_Format(format), m_MaxLevel(1), m_config(config)
{
	LoadFromFile(filepath, format);
}

Texture3D& Texture3D::SetFiltering(vk::Filter xFilter)
{
	if (m_config.minFilter == xFilter && m_config.magFilter == xFilter)
		return *this;

	m_config.minFilter = xFilter;
	m_config.magFilter = xFilter;
	CreateSampler();
	return *this;
}

Texture3D& Texture3D::SetFiltering(vk::Filter minFilter, vk::Filter magFilter)
{
	if (m_config.minFilter == minFilter && m_config.magFilter == magFilter)
		return *this;

	m_config.minFilter = minFilter;
	m_config.magFilter = magFilter;
	CreateSampler();
	return *this;
}

Texture3D& Texture3D::SetWrapping(vk::SamplerAddressMode wrap)
{
	if (m_config.wrapU == wrap && m_config.wrapV == wrap && m_config.wrapW == wrap)
		return *this;

	m_config.wrapU = wrap;
	m_config.wrapV = wrap;
	m_config.wrapW = wrap;
	CreateSampler();
	return *this;
}

Texture3D& Texture3D::SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW)
{
	if (m_config.wrapU == wrapU && m_config.wrapV == wrapV && m_config.wrapW == wrapW)
		return *this;

	m_config.wrapU = wrapU;
	m_config.wrapV = wrapV;
	m_config.wrapW = wrapW;
	CreateSampler();
	return *this;
}

Texture3D& Texture3D::SetAnisotropy(bool anisotropy)
{
	if (m_config.anisotropy == anisotropy)
		return *this;

	m_config.anisotropy = anisotropy;
	CreateSampler();
	return *this;
}

void Texture3D::UpdateTextureData(void* data, uint32_t level)
{
	if (!data || !_image)
		return;

	uint32_t maxLevelIdx = m_MaxLevel > 0 ? m_MaxLevel - 1 : 0;
	if (level > maxLevelIdx)
		return;

	_image->UploadData(data, level, 0);
}

bool Texture3D::LoadFromFile(const std::string& filepath, vk::Format format)
{
	DDSLoadResult result = LoadDDSFile(filepath, format);
	if (!result.valid || result.width == 0 || result.height == 0 || result.depth == 0)
		return false;

	// UploadData 会按 m_extent 算字节数，数据不够就会读越界，这里先挡住
	const size_t formatSize = static_cast<size_t>(ImageLayout::GetFormatSize(format));
	const size_t expectSize = static_cast<size_t>(result.width) * result.height * result.depth * formatSize;
	if (formatSize == 0 || result.data.size() < expectSize)
	{
		std::cout << std::format(
			"Texture3D::LoadFromFile data too small! got = {}, expect = {}, path = {}\n",
			result.data.size(), expectSize, filepath);
		return false;
	}

	m_Width = result.width;
	m_Height = result.height;
	m_Depth = result.depth;
	m_Format = format;

	if (!CreateImage())
		return false;

	CreateImageView();
	CreateSampler();

	return _image->UploadData(result.data.data(), 0);
}

void Texture3D::Resize(uint32_t width, uint32_t height, uint32_t depth)
{
	if (width == m_Width && height == m_Height && depth == m_Depth)
		return;

	m_Width = width;
	m_Height = height;
	m_Depth = depth;

	CreateImage();
	CreateImageView();
}

vk::Image Texture3D::GetImage() const
{
	return _image ? _image->GetHandle() : VK_NULL_HANDLE;
}

vk::ImageView Texture3D::GetImageView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetImageView(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

vk::ImageView Texture3D::GetImageView(const ImageViewInfo& info) const
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

vk::Sampler Texture3D::GetSampler() const
{
	return _sampler ? _sampler->GetHandle() : VK_NULL_HANDLE;
}

TextureDescBindEntry Texture3D::GetDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetDescBindEntry(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

TextureDescBindEntry Texture3D::GetDescBindEntry(const ImageViewInfo& info) const
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

uint32_t Texture3D::GetDescBindEntryVersion() const
{
	return _version;
}

uint32_t Texture3D::GetWidth() const
{
	return m_Width;
}

uint32_t Texture3D::GetHeight() const
{
	return m_Height;
}

uint32_t Texture3D::GetDepth() const
{
	return m_Depth;
}

uint32_t Texture3D::GetMaxLevel() const
{
	return m_MaxLevel;
}

glm::u32vec3 Texture3D::GetSize() const
{
	return glm::u32vec3(m_Width, m_Height, m_Depth);
}

bool Texture3D::IsEmpty() const
{
	return !_image || _image->GetHandle() == VK_NULL_HANDLE;
}

vk::Format Texture3D::GetFormat() const
{
	return m_Format;
}

vk::Filter Texture3D::GetMinFilter() const
{
	return m_config.minFilter;
}

vk::Filter Texture3D::GetMagFilter() const
{
	return m_config.magFilter;
}

vk::SamplerAddressMode Texture3D::GetWrapU() const
{
	return m_config.wrapU;
}

vk::SamplerAddressMode Texture3D::GetWrapV() const
{
	return m_config.wrapV;
}

vk::SamplerAddressMode Texture3D::GetWrapW() const
{
	return m_config.wrapW;
}

TextureConfig Texture3D::GetConfig() const
{
	return m_config;
}

vk::ImageLayout Texture3D::GetLayout(uint32_t level) const
{
	return _image->GetCurrentLayout(level);
}

void Texture3D::GenerateTextureMipMaps()
{
	if (m_MaxLevel <= 1)
		return;

	if (auto img = _image)
		img->GenerateMipMaps();
}

void Texture3D::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
{
	if (!cmd || !_image) return;

	vk::ImageLayout newLayout;
	vk::PipelineStageFlags dstStageMask;

	ImageLayout::GetImageLayoutAndStageFlag(&newLayout, &dstStageMask, m_Format, stage, usage);

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

void Texture3D::Barrier(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
{
	if (!cmd || !_image) return;

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

bool Texture3D::CreateImage()
{
	LockGuard guard(_mutex);

	auto image = std::make_shared<VKWrapper::VmaImage>();
	bool result = image->Create3D(VKCONTEXT->GetDevice().get(), m_Format, vk::Extent3D{ m_Width, m_Height, m_Depth }, m_MaxLevel);
	if (!result)
		return false;

	_image = image;
	_version++;

	return true;
}

bool Texture3D::CreateImageView()
{
	LockGuard guard(_mutex);
	_levelImageViews.clear();
	_version++;
	return true;
}

bool Texture3D::CreateSampler()
{
	LockGuard guard(_mutex);

	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo
		.setMinFilter(m_config.minFilter)
		.setMagFilter(m_config.magFilter)
		.setAddressModeU(m_config.wrapU)
		.setAddressModeV(m_config.wrapV)
		.setAddressModeW(m_config.wrapW)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMipLodBias(0.f)
		.setAnisotropyEnable(m_config.anisotropy)
		.setMinLod(0)
		.setMaxLod(FLT_MAX)
		.setBorderColor(vk::BorderColor::eFloatOpaqueBlack)
		.setUnnormalizedCoordinates(vk::False);

	if (m_config.anisotropy)
		samplerInfo.setMaxAnisotropy(TextureGeneralDef::GetAnisotropicTextureFiltering());

	auto sampler = std::make_shared<VKWrapper::VKSampler>();
	vk::Result result = sampler->Create(VKCONTEXT->GetDevice().get(), samplerInfo);
	if (result != vk::Result::eSuccess)
		return false;

	_sampler = sampler;
	_version++;

	return true;
}

std::shared_ptr<VKWrapper::VKImageView> Texture3D::CreateLevelImageView(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::e3D)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(ImageLayout::GetAspectMaskForFormat(m_Format))
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

std::shared_ptr<VKWrapper::BaseVKImage> Texture3D::GetImageSharedPtr() const
{
	return _image;
}
