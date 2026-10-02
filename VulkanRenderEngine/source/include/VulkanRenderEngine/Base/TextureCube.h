#pragma once

#include "Texture2DArray.h"
#include "CriticalSectionLock.h"

class TextureCube : public Texture2DArray
{
public:
	TextureCube(const std::array<std::string, 6>& filepaths, const TextureConfig& config = {}, bool autoMipMaps = false);
	TextureCube(uint32_t size, vk::Format format = vk::Format::eR8G8B8A8Unorm, const TextureConfig& config = {}, uint32_t maxLevel = 1);	// 创建空纹理

	vk::ImageView GetImageCubeView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = std::numeric_limits<uint32_t>::max()) const;
	vk::ImageView GetImageCubeView(const ImageViewInfo& info) const;
	TextureDescBindEntry GetCubeDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel = 0, uint32_t levelCount = UINT32_MAX) const;
	TextureDescBindEntry GetCubeDescBindEntry(const ImageViewInfo& info) const;

	bool LoadCubeFromFile(const std::array<std::string, 6>& filepaths, bool autoMipMaps);

private:
	virtual void OnCreateImageView() override;

	std::shared_ptr<VKWrapper::VKImageView> CreateLevelCubeView(const ImageViewInfo& info) const;

private:
	mutable std::unordered_map<ImageViewInfo, std::shared_ptr<VKWrapper::VKImageView>> _levelImageCubeViews;
};