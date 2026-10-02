#pragma once

#include "TextureGeneralDef.h"
#include "VulkanRenderEngine/SharedTexture.h"
#include "CriticalSectionLock.h"

struct SharedTexture;

class Texture2D
{

public:
	static void BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight, vk::Filter filterMode);
	static void BlitImage(Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight, vk::Filter filterMode);

	static bool BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest, vk::Filter filterMode);
	static bool BlitImageAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, vk::Filter filterMode);
	static bool BlitImage(Texture2D& src, Texture2D& dest, vk::Filter filterMode);
	static bool BlitImage(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, vk::Filter filterMode);

	static bool CopyTextureAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTextureAsync(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTexture(Texture2D& src, Texture2D& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);

public:
	Texture2D(const std::string& filepath, const TextureConfig& config = {}, bool autoMipMaps = false);																				// 从文件加载纹理
	Texture2D(uint32_t width, uint32_t height, vk::Format format = vk::Format::eR8G8B8A8Unorm, const TextureConfig& config = {}, uint32_t maxLevel = 1);	// 创建空纹理
	Texture2D(std::shared_ptr<SharedTexture> sharedTexture, const TextureConfig& config = {});			// 创建共享纹理

	~Texture2D();

public:
	Texture2D& SetFiltering(vk::Filter xFilter);
	Texture2D& SetFiltering(vk::Filter minFilter, vk::Filter magFilter);
	Texture2D& SetWrapping(vk::SamplerAddressMode wrapX);
	Texture2D& SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV);
	Texture2D& SetAnisotropy(bool anisotropy);

	void UpdateTextureData(void* data, uint32_t level = 0);
	bool LoadFromFile(const std::string& filepath, bool autoMipMaps);

	void Resize(uint32_t width, uint32_t height);			// 不保留数据！

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
	uint32_t GetMaxLevel() const;
	glm::u32vec2 GetSize() const;
	bool IsEmpty() const;

	vk::Format GetFormat() const;
	vk::Filter GetMinFilter() const;
	vk::Filter GetMagFilter() const;
	vk::SamplerAddressMode GetWrapU() const;
	vk::SamplerAddressMode GetWrapV() const;

	TextureConfig GetConfig() const;

	void GenerateTextureMipMaps();
public:
	std::shared_ptr<VKWrapper::BaseVKImage> GetImageSharedPtr() const;

private:
	bool CreateImage();
	bool CreateImageView();
	bool CreateSampler();

	std::shared_ptr<VKWrapper::VKImageView> CreateLevelImageView(const ImageViewInfo& info) const;

	bool CreateFromDX11SharedHandle(std::shared_ptr<SharedTexture> sharedTexture);

private:
	std::shared_ptr<VKWrapper::BaseVKImage> _image;
	std::shared_ptr<VKWrapper::VKSampler> _sampler;

	mutable std::unordered_map<ImageViewInfo, std::shared_ptr<VKWrapper::VKImageView>> _levelImageViews;

	mutable CriticalSectionLock _mutex;

	std::atomic<uint32_t> _version{ 0 };

	uint32_t m_Width;
	uint32_t m_Height;
	vk::Format m_Format;
	uint32_t m_MaxLevel;
	TextureConfig m_config;
};

class ITextureArrayProvider
{
public:
	virtual std::vector<TextureDescBindEntry> GetTextureDescBindEntrys()const = 0;

protected:
	vk::ImageAspectFlags _aspect;
};

class BaseTextureArrayProvider :public ITextureArrayProvider
{
public:
	BaseTextureArrayProvider(const std::vector<std::shared_ptr<Texture2D>>& texs, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
		: _texs(texs) {
		_aspect = aspect;
	}

	virtual std::vector<TextureDescBindEntry> GetTextureDescBindEntrys()const
	{
		std::vector<TextureDescBindEntry> result;
		result.reserve(_texs.size());
		for (auto& tex : _texs)
			result.push_back(std::move(tex->GetDescBindEntry(_aspect)));
		return result;
	}

private:
	std::vector<std::shared_ptr<Texture2D>> _texs;
};
