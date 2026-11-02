#pragma once

#include "TextureGeneralDef.h"
#include "CriticalSectionLock.h"


class Texture3D
{

public:
	Texture3D(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		vk::Format format = vk::Format::eR8G8B8A8Unorm,
		const TextureConfig& config = {},
		uint32_t maxLevel = 1
	);

	~Texture3D() = default;

	// 从 DDS 体积纹理加载。只有 DDS 能描述 3D 数据
	Texture3D(
		const std::string& filepath,
		vk::Format format = vk::Format::eR8G8B8A8Unorm,
		const TextureConfig& config = {}
	);

public:
	Texture3D& SetFiltering(vk::Filter xFilter);
	Texture3D& SetFiltering(vk::Filter minFilter, vk::Filter magFilter);
	Texture3D& SetWrapping(vk::SamplerAddressMode wrap);
	Texture3D& SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV, vk::SamplerAddressMode wrapW);
	Texture3D& SetAnisotropy(bool anisotropy);

	// 上传某 mip 的整块体数据，数据布局为紧密排列的 width*height*depth
	void UpdateTextureData(void* data, uint32_t level = 0);

	// 从 DDS 体积纹理加载，成功时会按文件尺寸重建 GPU 资源并覆盖当前的尺寸/格式
	bool LoadFromFile(const std::string& filepath, vk::Format format = vk::Format::eR8G8B8A8Unorm);

	void Resize(uint32_t width, uint32_t height, uint32_t depth);	// 不保留数据！

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

	vk::Image GetImage() const;
	vk::ImageView GetImageView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = std::numeric_limits<uint32_t>::max()) const;
	vk::ImageView GetImageView(const ImageViewInfo& info) const;
	vk::Sampler GetSampler() const;
	TextureDescBindEntry GetDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = UINT32_MAX) const;
	TextureDescBindEntry GetDescBindEntry(const ImageViewInfo& info) const;
	uint32_t GetDescBindEntryVersion() const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetDepth() const;
	uint32_t GetMaxLevel() const;
	glm::u32vec3 GetSize() const;
	bool IsEmpty() const;

	vk::Format GetFormat() const;
	vk::Filter GetMinFilter() const;
	vk::Filter GetMagFilter() const;
	vk::SamplerAddressMode GetWrapU() const;
	vk::SamplerAddressMode GetWrapV() const;
	vk::SamplerAddressMode GetWrapW() const;

	TextureConfig GetConfig() const;

	vk::ImageLayout GetLayout(uint32_t level = 0) const;

	void GenerateTextureMipMaps();

public:
	std::shared_ptr<VKWrapper::BaseVKImage> GetImageSharedPtr() const;

private:
	bool CreateImage();
	bool CreateImageView();
	bool CreateSampler();

	std::shared_ptr<VKWrapper::VKImageView> CreateLevelImageView(const ImageViewInfo& info) const;

private:
	std::shared_ptr<VKWrapper::BaseVKImage> _image;
	std::shared_ptr<VKWrapper::VKSampler> _sampler;

	mutable std::unordered_map<ImageViewInfo, std::shared_ptr<VKWrapper::VKImageView>> _levelImageViews;

	mutable CriticalSectionLock _mutex;

	std::atomic<uint32_t> _version{ 0 };

	uint32_t m_Width;
	uint32_t m_Height;
	uint32_t m_Depth;
	vk::Format m_Format;
	uint32_t m_MaxLevel;
	TextureConfig m_config;
};
