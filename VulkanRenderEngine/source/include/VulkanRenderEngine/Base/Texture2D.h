#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/SharedTexture.h"
#include "CriticalSectionLock.h"

struct SharedTexture;

struct Texture2DConfig
{
	vk::Filter minFilter = vk::Filter::eNearest;
	vk::Filter magFilter = vk::Filter::eNearest;

	vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eClampToEdge;
	vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eClampToEdge;

	bool anisotropy = false;
	bool gammaCorrection = false;

	bool operator==(const Texture2DConfig& other);
	bool operator!=(const Texture2DConfig& other);
};

struct TextureDescBindEntry
{
	std::shared_ptr<VKWrapper::BaseVKImage> image;
	std::shared_ptr<VKWrapper::VKImageView> imageView;
	std::shared_ptr<VKWrapper::VKSampler> sampler;
	uint32_t version;
};

struct LevelInfo {
	uint32_t base = 0;
	uint32_t count = 1;
	bool operator==(const LevelInfo& other) const { return base == other.base && count == other.count; }
};


namespace std {
	template<> struct hash<LevelInfo> {
		size_t operator()(const LevelInfo& info) const {
			uint64_t combined = (static_cast<uint64_t>(info.base) << 32) | info.count;
			return std::hash<uint64_t>{}(combined);
		}
	};
}

class Texture2D
{

public:
	enum class BindStage { Graphics = 0, Compute, RayTracing };	// 使用场景，图像渲染管线、计算管线、光追管线
	enum class BindUsage { Output = 0, Sample };				// 用途,作为管线输出(Graphics中的附件，Compute中imagestore的对象)，还是输入的采样纹理

public:
	static void GetImageLayoutAndStageFlag(vk::ImageLayout* outLayout, vk::PipelineStageFlags* outDestStageFlag, vk::Format format, BindStage stage = BindStage::Graphics, BindUsage usage = BindUsage::Sample);

	static void BlitImageAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight);
	static void BlitImage(Texture2D& src, vk::Image dstImage, uint32_t dstWidth, uint32_t dstHeight);

	static bool BlitImageAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest);
	static bool BlitImageAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest);
	static bool BlitImage(Texture2D& src, Texture2D& dest);
	static bool BlitImage(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest);

	static bool CopyTextureAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, Texture2D& src, Texture2D& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTextureAsync(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTexture(Texture2D& src, Texture2D& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);

public:
	Texture2D(const std::string& filepath, const Texture2DConfig& config = {});																					// 从文件加载纹理
	Texture2D(uint32_t width, uint32_t height, vk::Format format = vk::Format::eR8G8B8A8Unorm, const Texture2DConfig& config = {}, uint32_t maxLevel = 1);	// 创建空纹理
	Texture2D(std::shared_ptr<SharedTexture> sharedTexture, const Texture2DConfig& config = {});			// 创建共享纹理

	~Texture2D();

public:
	Texture2D& SetFiltering(vk::Filter xFilter);
	Texture2D& SetFiltering(vk::Filter minFilter, vk::Filter magFilter);
	Texture2D& SetWrapping(vk::SamplerAddressMode wrapX);
	Texture2D& SetWrapping(vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV);
	Texture2D& SetAnisotropy(bool anisotropy);

	void UpdateTextureData(void* data, uint32_t level = 0);
	bool LoadFromFile(const std::string& filepath);

	void Resize(uint32_t width, uint32_t height);			// 不保留数据！

public:
	void TransitionLayout(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, vk::ImageLayout layout, vk::PipelineStageFlags dstAccessMask, uint32_t level = UINT32_MAX);

	void TransitionLayout(
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
		vk::ImageLayout* outLayout = nullptr,
		BindStage stage = BindStage::Graphics, BindUsage usage = BindUsage::Sample,
		uint32_t level = UINT32_MAX
	);

	void Barrier(
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
		vk::ImageLayout* outLayout = nullptr,
		BindStage stage = BindStage::Graphics, BindUsage usage = BindUsage::Sample,
		uint32_t level = UINT32_MAX
	);

	vk::Image GetImage() const;
	vk::ImageView GetImageView(uint32_t baseMipLevel = 0, uint32_t levelCount = UINT32_MAX) const;
	vk::Sampler GetSampler() const;
	TextureDescBindEntry GetDescBindEntry(uint32_t baseMipLevel = 0, uint32_t levelCount = UINT32_MAX) const;
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

	Texture2DConfig GetConfig() const;

private:
	bool CreateImage();
	bool CreateImageView();
	bool CreateSampler();

	std::shared_ptr<VKWrapper::VKImageView> CreateLevelImageView(uint32_t BaseMipLevel, uint32_t LevelCount) const;

	bool CreateFromDX11SharedHandle(std::shared_ptr<SharedTexture> sharedTexture);

private:
	std::shared_ptr<VKWrapper::BaseVKImage> _image;
	std::shared_ptr<VKWrapper::VKImageView> _imageView;
	std::shared_ptr<VKWrapper::VKSampler> _sampler;

	mutable std::unordered_map<LevelInfo, std::shared_ptr<VKWrapper::VKImageView>> _levelImageViews;

	mutable CriticalSectionLock _mutex;

	std::atomic<uint32_t> _version{ 0 };

	uint32_t m_Width;
	uint32_t m_Height;
	vk::Format m_Format;
	uint32_t m_MaxLevel;
	Texture2DConfig m_config;
};

class ITextureArrayProvider
{
public:
	virtual std::vector<TextureDescBindEntry> GetTextureDescBindEntrys()const = 0;
};

class BaseTextureArrayProvider :public ITextureArrayProvider
{
public:
	static std::shared_ptr<BaseTextureArrayProvider> Create(const std::vector<std::shared_ptr<Texture2D>>& texs) {
		return std::make_shared<BaseTextureArrayProvider>(texs);
	}

	BaseTextureArrayProvider(const std::vector<std::shared_ptr<Texture2D>>& texs)
		: _texs(texs) {}

	virtual std::vector<TextureDescBindEntry> GetTextureDescBindEntrys()const
	{
		std::vector<TextureDescBindEntry> result;
		result.reserve(_texs.size());
		for (auto& tex : _texs)
			result.push_back(std::move(tex->GetDescBindEntry()));
		return result;
	}

private:
	std::vector<std::shared_ptr<Texture2D>> _texs;
};
