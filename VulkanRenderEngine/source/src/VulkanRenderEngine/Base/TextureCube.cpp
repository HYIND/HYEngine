#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/VKContext.h"

TextureCube::TextureCube(const std::array<std::string, 6>& filepaths, const TextureConfig& config, bool autoMipMaps)
	:Texture2DArray(std::vector<std::string>(filepaths.begin(), filepaths.end()), config, autoMipMaps)
{}

TextureCube::TextureCube(uint32_t size, vk::Format format, const TextureConfig& config, uint32_t maxLevel)
	:Texture2DArray(size, size, format, config, 6, maxLevel)
{}

vk::ImageView TextureCube::GetImageCubeView(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetImageCubeView(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

vk::ImageView TextureCube::GetImageCubeView(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);
	if (auto it = _levelImageCubeViews.find(info); it != _levelImageCubeViews.end())
		return it->second->GetHandle();
	else
	{
		auto levelImageCubeView = CreateLevelCubeView(info);
		if (!levelImageCubeView)
			return VK_NULL_HANDLE;
		_levelImageCubeViews[info] = levelImageCubeView;
		return levelImageCubeView->GetHandle();
	}
}

TextureDescBindEntry TextureCube::GetCubeDescBindEntry(vk::ImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount) const
{
	return GetCubeDescBindEntry(ImageViewInfo{ .aspect = aspect, .base = baseMipLevel, .count = levelCount });
}

TextureDescBindEntry TextureCube::GetCubeDescBindEntry(const ImageViewInfo& info) const
{
	LockGuard guard(_mutex);
	if (auto it = _levelImageCubeViews.find(info); it != _levelImageCubeViews.end())
		return TextureDescBindEntry{ .image = _image, .imageView = it->second, .sampler = _sampler, .version = _version };
	else
	{
		auto levelImageCubeView = CreateLevelCubeView(info);
		if (!levelImageCubeView)
			return TextureDescBindEntry{ .image = _image, .imageView = nullptr, .sampler = _sampler, .version = _version };
		_levelImageCubeViews[info] = levelImageCubeView;
		return TextureDescBindEntry{ .image = _image, .imageView = levelImageCubeView, .sampler = _sampler, .version = _version };
	}
}

bool TextureCube::LoadCubeFromFile(const std::array<std::string, 6>& filepaths, bool autoMipMaps)
{
	return LoadFromFile(std::vector<std::string>(filepaths.begin(), filepaths.end()), autoMipMaps);
}

void TextureCube::OnCreateImageView() {
	_levelImageCubeViews.clear();
}

std::shared_ptr<VKWrapper::VKImageView> TextureCube::CreateLevelCubeView(const ImageViewInfo& info) const
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
