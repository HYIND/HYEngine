#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Texture2DArray.h"
#include "VulkanRenderEngine/VKContext.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>
#include "VulkanRenderEngine/General/DDSLoader.h"

Texture2DArray::Texture2DArray(const std::vector<std::string>& filepaths, const TextureConfig& config, bool autoMipMaps)
	: m_Width(0), m_Height(0), m_layerCount(0), m_MaxLevel(1u), m_config(config)
{
	LoadFromFile(filepaths, autoMipMaps);
}

Texture2DArray::Texture2DArray(uint32_t width, uint32_t height, vk::Format format, const TextureConfig& config, uint32_t layerCount, uint32_t level)
	: m_Width(width), m_Height(height), m_layerCount(layerCount), m_Format(format), m_MaxLevel(std::max(1u, level)), m_config(config)
{
	CreateImage();
	CreateImageView();
	CreateSampler();
}

Texture2DArray& Texture2DArray::SetFiltering(vk::Filter xFilter)
{
	if (m_config.minFilter == xFilter && m_config.magFilter == xFilter)
		return *this;

	m_config.minFilter = xFilter;
	m_config.magFilter = xFilter;
	CreateSampler();
	return *this;
}

Texture2DArray& Texture2DArray::SetFiltering(vk::Filter minFilter, vk::Filter magFilter)
{
	if (m_config.minFilter == minFilter && m_config.magFilter == magFilter)
		return *this;

	m_config.minFilter = minFilter;
	m_config.magFilter = magFilter;
	CreateSampler();
	return *this;
}

Texture2DArray& Texture2DArray::SetWrapping(vk::SamplerAddressMode wrap)
{
	if (m_config.wrapU == wrap && m_config.wrapV == wrap && m_config.wrapW == wrap)
		return *this;

	m_config.wrapU = wrap;
	m_config.wrapV = wrap;
	m_config.wrapW = wrap;
	CreateSampler();
	return *this;
}

Texture2DArray& Texture2DArray::SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW)
{
	if (m_config.wrapU == wrapU && m_config.wrapV == wrapV && m_config.wrapW == wrapW)
		return *this;

	m_config.wrapU = wrapU;
	m_config.wrapV = wrapV;
	m_config.wrapW = wrapW;
	CreateSampler();
	return *this;
}

Texture2DArray& Texture2DArray::SetAnisotropy(bool anisotropy)
{
	if (m_config.anisotropy == anisotropy)
		return *this;

	m_config.anisotropy = anisotropy;
	CreateSampler();
	return *this;
}

bool Texture2DArray::LoadFromFile(const std::vector<std::string>& filepaths, bool autoMipMaps)
{

	LockGuard guard(_mutex);

	auto LoadFromData = [&](std::vector<unsigned char*> datas, uint32_t width, uint32_t height, vk::Format format, uint32_t dataComponents) ->bool
		{
			m_Width = width;
			m_Height = height;
			m_layerCount = datas.size();
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

			for (uint32_t i = 0; i < datas.size(); i++)
			{
				auto& data = datas[i];
				vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(width * height * dataComponents);
				if (!_image->UploadData(data, 0, i))
					return false;
			}

			if (m_MaxLevel > 1)
				GenerateTextureMipMaps();

			return true;
		};

	uint32_t width = 0;
	uint32_t height = 0;
	vk::Format format = vk::Format::eUndefined;

	auto checkSameLayout = [&](uint32_t w, uint32_t h, vk::Format f) -> bool {
		if (width == 0 && height == 0 && format == vk::Format::eUndefined)
		{
			width = w;
			height = h;
			format = f;
			return true;
		}

		return width == w && height == h && format == f;
		};

	std::vector<DDSLoadResult> ddsDatas;
	std::vector<unsigned char*> stbiDatas;
	std::vector<unsigned char*> datas;

	bool sameLayout = true;

	for (auto& filepath : filepaths)
	{
		if (Tool::ToLower(fs::path(filepath).extension().string()) == ".dds")
		{
			vk::Format format = m_config.gammaCorrection ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
			DDSLoadResult result = LoadDDSFile(filepath, format);
			if (!result.valid)
			{
				//std::cout << "Texture failed to load at path: " << filepath << std::endl;
				return false;
			}
			if (!checkSameLayout(result.width, result.height, result.format))
			{
				sameLayout = false;
				break;
			}
			datas.push_back(result.data.data());
			ddsDatas.push_back(std::move(result));
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

			if (!checkSameLayout(width, height, format))
			{
				sameLayout = false;
				break;
			}
			datas.push_back(data);
			stbiDatas.push_back(data);
		}
	}

	if (!sameLayout)
		return false;

	bool result = LoadFromData(datas, width, height, format, ImageLayout::GetFormatSize(format));

	for (auto data : stbiDatas)
		stbi_image_free(data);

	return result;
}

vk::Image Texture2DArray::GetImage() const
{
	return _image->GetHandle();
}

vk::ImageView Texture2DArray::GetImageView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetImageView(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

vk::ImageView Texture2DArray::GetImageView(const ImageViewInfo& info) const
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

vk::Sampler Texture2DArray::GetSampler() const
{
	return _sampler->GetHandle();
}

TextureDescBindEntry Texture2DArray::GetDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetDescBindEntry(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

TextureDescBindEntry Texture2DArray::GetDescBindEntry(const ImageViewInfo& info) const
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
uint32_t Texture2DArray::GetDescBindEntryVersion() const
{
	return _version;
}

uint32_t Texture2DArray::GetWidth() const
{
	return m_Width;
}

uint32_t Texture2DArray::GetHeight() const
{
	return m_Height;
}

uint32_t Texture2DArray::GetLayerCount() const
{
	return m_layerCount;
}

uint32_t Texture2DArray::GetMaxLevel() const
{
	return m_MaxLevel;
}

glm::u32vec2 Texture2DArray::GetSize() const
{
	return glm::u32vec2(m_Width, m_Height);
}

bool Texture2DArray::IsEmpty() const
{
	return !_image || _image->GetHandle() == VK_NULL_HANDLE;
}

vk::Format Texture2DArray::GetFormat() const
{
	return m_Format;
}

vk::Filter Texture2DArray::GetMinFilter() const
{
	return m_config.minFilter;
}

vk::Filter Texture2DArray::GetMagFilter() const
{
	return m_config.magFilter;
}

vk::SamplerAddressMode Texture2DArray::GetWrapU() const
{
	return m_config.wrapU;
}

vk::SamplerAddressMode Texture2DArray::GetWrapV() const
{
	return m_config.wrapV;
}

vk::SamplerAddressMode Texture2DArray::GetWrapW() const
{
	return m_config.wrapW;
}

TextureConfig Texture2DArray::GetConfig() const
{
	return m_config;
}

void Texture2DArray::GenerateTextureMipMaps()
{
	if (m_MaxLevel <= 1)
		return;

	if (auto img = _image)
		img->GenerateMipMaps();
}

void Texture2DArray::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
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

void Texture2DArray::Barrier(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
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

bool Texture2DArray::CreateImage()
{
	LockGuard guard(_mutex);

	auto image = std::make_shared<VKWrapper::VmaImage>();
	bool result = image->Create(VKCONTEXT->GetDevice().get(), m_Format, { m_Width ,m_Height }, m_MaxLevel, m_layerCount);
	if (!result)
		return false;

	_image = image;
	_version++;

	return true;
}

bool Texture2DArray::CreateImageView()
{
	LockGuard guard(_mutex);
	_levelImageViews.clear();
	_version++;
	OnCreateImageView();
	return true;
}

bool Texture2DArray::CreateSampler()
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

std::shared_ptr<VKWrapper::VKImageView> Texture2DArray::CreateLevelImageView(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::e2DArray)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(ImageLayout::GetAspectMaskForFormat(m_Format))
			.setBaseMipLevel(info.base)
			.setLevelCount(info.count)
			.setBaseArrayLayer(0)
			.setLayerCount(m_layerCount)
		);

	auto imageView = std::make_shared<VKWrapper::VKImageView>();
	vk::Result result = imageView->Create(VKCONTEXT->GetDevice().get(), viewInfo);
	if (result != vk::Result::eSuccess)
		return nullptr;

	return imageView;
}
