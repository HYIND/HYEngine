#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/VKContext.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>
#include <iostream>

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

bool TextureCubeConfig::operator==(const TextureCubeConfig& other) const
{
	return minFilter == other.minFilter
		&& magFilter == other.magFilter
		&& wrapU == other.wrapU
		&& wrapV == other.wrapV
		&& wrapW == other.wrapW
		&& anisotropy == other.anisotropy
		&& gammaCorrection == other.gammaCorrection;
}

bool TextureCubeConfig::operator!=(const TextureCubeConfig& other) const
{
	return !(*this == other);
}

TextureCube::TextureCube(const std::array<std::string, 6>& filepaths, const TextureCubeConfig& config)
	: m_config(config)
{
	LoadFromFile(filepaths);
}

TextureCube& TextureCube::SetFiltering(vk::Filter xFilter)
{
	if (m_config.minFilter == xFilter && m_config.magFilter == xFilter)
		return *this;

	m_config.minFilter = xFilter;
	m_config.magFilter = xFilter;
	CreateSampler();
	return *this;
}

TextureCube& TextureCube::SetFiltering(vk::Filter minFilter, vk::Filter magFilter)
{
	if (m_config.minFilter == minFilter && m_config.magFilter == magFilter)
		return *this;

	m_config.minFilter = minFilter;
	m_config.magFilter = magFilter;
	CreateSampler();
	return *this;
}

TextureCube& TextureCube::SetWrapping(vk::SamplerAddressMode wrap)
{
	if (m_config.wrapU == wrap && m_config.wrapV == wrap && m_config.wrapW == wrap)
		return *this;

	m_config.wrapU = wrap;
	m_config.wrapV = wrap;
	m_config.wrapW = wrap;
	CreateSampler();
	return *this;
}

TextureCube& TextureCube::SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW)
{
	if (m_config.wrapU == wrapU && m_config.wrapV == wrapV && m_config.wrapW == wrapW)
		return *this;

	m_config.wrapU = wrapU;
	m_config.wrapV = wrapV;
	m_config.wrapW = wrapW;
	CreateSampler();
	return *this;
}

TextureCube& TextureCube::SetAnisotropy(bool anisotropy)
{
	if (m_config.anisotropy == anisotropy)
		return *this;

	m_config.anisotropy = anisotropy;
	CreateSampler();
	return *this;
}

bool TextureCube::LoadFromFile(const std::array<std::string, 6>& filepaths)
{
	int reqComponents = 4;
	int width, height, realComponents;
	unsigned char* firstData = stbi_load(filepaths[0].c_str(), &width, &height, &realComponents, reqComponents);
	if (!firstData)
	{
		std::cout << "Cubemap texture failed to load at path: " << filepaths[0] << std::endl;
		return false;
	}
	stbi_image_free(firstData);

	// 所有面必须尺寸一致
	for (unsigned int i = 1; i < filepaths.size(); i++)
	{
		int w, h, comp;
		unsigned char* data = stbi_load(filepaths[i].c_str(), &w, &h, &comp, reqComponents);
		if (!data || w != width || h != height)
		{
			if (data) stbi_image_free(data);
			std::cout << "Cubemap faces must have same size!" << std::endl;
			return false;
		}
		stbi_image_free(data);
	}

	int dataComponents = reqComponents > 0 ? reqComponents : realComponents;

	// 确定格式
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
		return false;
	}

	m_Size = static_cast<uint32_t>(width);
	m_Format = format;

	// 创建图像（Cubemap：arrayLayers = 6）
	if (!CreateImage())
		return false;

	// 创建 View 和 Sampler
	if (!CreateImageView() || !CreateSampler())
		return false;

	// 逐面加载并上传
	for (unsigned int i = 0; i < filepaths.size(); i++)
	{
		unsigned char* data = stbi_load(filepaths[i].c_str(), &width, &height, &realComponents, dataComponents);
		if (!data)
		{
			std::cout << "Cubemap texture failed to load at path: " << filepaths[i] << std::endl;
			continue;
		}

		vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(width * height * dataComponents);
		if (!_image->UploadData(data, 0, i)) {
			stbi_image_free(data);
			return false;
		}

		stbi_image_free(data);
	}

	return true;
}

vk::Image TextureCube::GetImage() const
{
	return _image->GetHandle();
}

vk::ImageView TextureCube::GetImageView() const
{
	return _imageView->GetHandle();
}

vk::Sampler TextureCube::GetSampler() const
{
	return _sampler->GetHandle();
}

TextureCubeDescBindEntry TextureCube::GetDescBindEntry() const
{
	LockGuard guard(_mutex);
	return TextureCubeDescBindEntry{ .image = _image, .imageView = _imageView, .sampler = _sampler, .version = _version };
}

uint32_t TextureCube::GetDescBindEntryVersion() const
{
	return _version;
}

uint32_t TextureCube::GetWidth() const
{
	return m_Size;
}

uint32_t TextureCube::GetHeight() const
{
	return m_Size;
}

glm::u32vec2 TextureCube::GetSize() const
{
	return glm::u32vec2(m_Size, m_Size);
}

bool TextureCube::IsEmpty() const
{
	return !_image || _image->GetHandle() == VK_NULL_HANDLE;
}

vk::Format TextureCube::GetFormat() const
{
	return m_Format;
}

vk::Filter TextureCube::GetMinFilter() const
{
	return m_config.minFilter;
}

vk::Filter TextureCube::GetMagFilter() const
{
	return m_config.magFilter;
}

vk::SamplerAddressMode TextureCube::GetWrapU() const
{
	return m_config.wrapU;
}

vk::SamplerAddressMode TextureCube::GetWrapV() const
{
	return m_config.wrapV;
}

vk::SamplerAddressMode TextureCube::GetWrapW() const
{
	return m_config.wrapW;
}

TextureCubeConfig TextureCube::GetConfig() const
{
	return m_config;
}

void TextureCube::TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout* outLayout, ImageLayout::BindStage stage, ImageLayout::BindUsage usage, uint32_t level)
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

bool TextureCube::CreateImage()
{
	LockGuard guard(_mutex);

	auto image = std::make_shared<VKWrapper::VmaImage>();
	bool result = image->Create(VKCONTEXT->GetDevice().get(), m_Format, { m_Size ,m_Size }, 1, VKWrapper::VmaImage::ImageType::ImageCube);
	if (!result)
		return false;

	_image = image;
	_version++;

	return true;
}

bool TextureCube::CreateImageView()
{
	LockGuard guard(_mutex);

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo
		.setImage(_image->GetHandle())
		.setViewType(vk::ImageViewType::eCube)
		.setFormat(m_Format)
		.setSubresourceRange(
			vk::ImageSubresourceRange()
			.setAspectMask(ImageLayout::GetAspectMaskForFormat(m_Format))
			.setBaseMipLevel(0)
			.setLevelCount(1)
			.setBaseArrayLayer(0)
			.setLayerCount(6)
		);

	auto imageView = std::make_shared<VKWrapper::VKImageView>();
	vk::Result result = imageView->Create(VKCONTEXT->GetDevice().get(), viewInfo);
	if (result != vk::Result::eSuccess)
		return false;

	_imageView = imageView;
	_version++;

	return true;
}

bool TextureCube::CreateSampler()
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
		samplerInfo.setMaxAnisotropy(GetAnisotropicTextureFiltering());

	auto sampler = std::make_shared<VKWrapper::VKSampler>();
	vk::Result result = sampler->Create(VKCONTEXT->GetDevice().get(), samplerInfo);
	if (result != vk::Result::eSuccess)
		return false;

	_sampler = sampler;
	_version++;

	return true;
}