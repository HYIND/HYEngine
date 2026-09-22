#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/General/ImageLayoutWrapper.h"
#include "CriticalSectionLock.h"

struct TextureCubeConfig
{
	vk::Filter minFilter = vk::Filter::eNearest;
	vk::Filter magFilter = vk::Filter::eNearest;
	vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eClampToEdge;
	vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eClampToEdge;
	vk::SamplerAddressMode wrapW = vk::SamplerAddressMode::eClampToEdge;
	bool anisotropy = false;
	bool gammaCorrection = true;

	bool operator==(const TextureCubeConfig& other) const;
	bool operator!=(const TextureCubeConfig& other) const;
};

struct TextureCubeDescBindEntry
{
	std::shared_ptr<VKWrapper::VmaImage> image;
	std::shared_ptr<VKWrapper::VKImageView> imageView;
	std::shared_ptr<VKWrapper::VKSampler> sampler;
	uint32_t version;
};

class TextureCube
{

public:
	TextureCube(const std::array<std::string, 6>& filepaths, const TextureCubeConfig& config = {});
	~TextureCube() = default;

public:
	TextureCube& SetFiltering(vk::Filter xFilter);
	TextureCube& SetFiltering(vk::Filter minFilter, vk::Filter magFilter);
	TextureCube& SetWrapping(vk::SamplerAddressMode wrap);
	TextureCube& SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW);
	TextureCube& SetAnisotropy(bool anisotropy);

	bool LoadFromFile(const std::array<std::string, 6>& filepaths);

public:
	vk::Image GetImage() const;
	vk::ImageView GetImageView() const;
	vk::Sampler GetSampler() const;
	TextureCubeDescBindEntry GetDescBindEntry() const;
	uint32_t GetDescBindEntryVersion() const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetMaxLevel() const;
	glm::u32vec2 GetSize() const;
	bool IsEmpty() const;

	vk::Format GetFormat() const;
	vk::Filter GetMinFilter() const;
	vk::Filter GetMagFilter() const;
	vk::SamplerAddressMode GetWrapU() const;
	vk::SamplerAddressMode GetWrapV() const;
	vk::SamplerAddressMode GetWrapW() const;

	TextureCubeConfig GetConfig() const;

public:
	void TransitionLayout(
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
		vk::ImageLayout* outLayout,
		ImageLayout::BindStage stage, ImageLayout::BindUsage usage,
		uint32_t level = UINT32_MAX
	);

private:
	bool CreateImage();
	bool CreateImageView();
	bool CreateSampler();

private:
	std::shared_ptr<VKWrapper::VmaImage> _image;
	std::shared_ptr<VKWrapper::VKImageView> _imageView;
	std::shared_ptr<VKWrapper::VKSampler> _sampler;

	mutable CriticalSectionLock _mutex;

	std::atomic<uint32_t> _version{ 0 };

	uint32_t m_Size;
	vk::Format m_Format;
	TextureCubeConfig m_config;
};
