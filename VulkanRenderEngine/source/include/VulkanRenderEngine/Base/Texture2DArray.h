#pragma once

#include "TextureGeneralDef.h"
#include "CriticalSectionLock.h"

class Texture2DArray
{

public:
	Texture2DArray(const std::vector<std::string>& filepaths, const TextureConfig& config = {}, bool autoMipMaps = false);																				// 从文件加载纹理
	Texture2DArray(uint32_t width, uint32_t height, vk::Format format = vk::Format::eR8G8B8A8Unorm, const TextureConfig& config = {}, uint32_t layerCount = 1, uint32_t maxLevel = 1);	// 创建空纹理

public:
	Texture2DArray& SetFiltering(vk::Filter xFilter);
	Texture2DArray& SetFiltering(vk::Filter minFilter, vk::Filter magFilter);
	Texture2DArray& SetWrapping(vk::SamplerAddressMode wrap);
	Texture2DArray& SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW);
	Texture2DArray& SetAnisotropy(bool anisotropy);

	bool LoadFromFile(const std::vector<std::string>& filepaths, bool autoMipMaps);

public:
	vk::Image GetImage() const;
	vk::ImageView GetImageView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = std::numeric_limits<uint32_t>::max()) const;
	vk::ImageView GetImageView(const ImageViewInfo& info) const;
	vk::Sampler GetSampler() const;
	TextureDescBindEntry GetDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = UINT32_MAX) const;
	TextureDescBindEntry GetDescBindEntry(const ImageViewInfo& info) const;
	uint32_t GetDescBindEntryVersion() const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetLayerCount() const;
	uint32_t GetMaxLevel() const;
	glm::u32vec2 GetSize() const;
	bool IsEmpty() const;

	vk::Format GetFormat() const;
	vk::Filter GetMinFilter() const;
	vk::Filter GetMagFilter() const;
	vk::SamplerAddressMode GetWrapU() const;
	vk::SamplerAddressMode GetWrapV() const;
	vk::SamplerAddressMode GetWrapW() const;

	TextureConfig GetConfig() const;

	void GenerateTextureMipMaps();
	void GenerateTextureMipMapsAsync(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);

	// 生成完内容后调一次：所有层、所有 mip 固定为 SHADER_READ_ONLY_OPTIMAL，之后可以在不同线程 / 不同帧同时采样，
	// 写入、生成 mip、上传都会被拒绝（同 Texture2D::MakeSampledImmutable，见 BaseVKImage::MakeSampledImmutable）
	bool MakeSampledImmutable();
	bool IsSampledImmutable() const;
public:
	void TransitionLayout(
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
		vk::ImageLayout* outLayout,
		ImageLayout::BindStage stage, ImageLayout::BindUsage usage,
		uint32_t level = UINT32_MAX
	);

	void Barrier(
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
		vk::ImageLayout* outLayout,
		ImageLayout::BindStage stage, ImageLayout::BindUsage usage,
		uint32_t level = UINT32_MAX
	);

protected:
	virtual void OnCreateImageView() {}

protected:
	bool CreateImage();
	bool CreateImageView();
	bool CreateSampler();

	std::shared_ptr<VKWrapper::VKImageView> CreateLevelImageView(const ImageViewInfo& info) const;

protected:
	std::shared_ptr<VKWrapper::BaseVKImage> _image;
	std::shared_ptr<VKWrapper::VKSampler> _sampler;

	mutable std::unordered_map<ImageViewInfo, std::shared_ptr<VKWrapper::VKImageView>> _levelImageViews;

	mutable CriticalSectionLock _mutex;

	std::atomic<uint32_t> _version{ 0 };

	uint32_t m_Width;
	uint32_t m_Height;
	uint32_t m_layerCount;
	vk::Format m_Format;
	uint32_t m_MaxLevel;
	TextureConfig m_config;
};
