#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/General/ImageLayoutWrapper.h"


struct ImageViewInfo {
	vk::ImageAspectFlags aspect;
	uint32_t base = 0;
	uint32_t count = 1;
	bool operator==(const ImageViewInfo& other) const { return aspect == other.aspect && base == other.base && count == other.count; }
};

namespace std {
	template<> struct hash<ImageViewInfo> {
		size_t operator()(const ImageViewInfo& info) const noexcept {
			uint64_t hash = (static_cast<uint64_t>(info.base) << 32) | info.count;
			hash ^= (static_cast<uint64_t>((uint32_t)info.aspect)) * 0x9E3779B97F4A7C15ull;
			return std::hash<uint64_t>{}(hash);
		}
	};
}

struct TextureConfig
{
	vk::Filter minFilter = vk::Filter::eNearest;
	vk::Filter magFilter = vk::Filter::eNearest;

	vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eClampToEdge;
	vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eClampToEdge;

	bool anisotropy = false;
	bool gammaCorrection = false;

	vk::SamplerAddressMode wrapW = vk::SamplerAddressMode::eClampToEdge;	//仅Texture2dArray中作为TextureCube的时候使用

	bool operator==(const TextureConfig& other) const;
	bool operator!=(const TextureConfig& other) const;

	static TextureConfig GetDefaultSkyCubeConfig() {
		return TextureConfig{
			.minFilter = vk::Filter::eLinear,
			.magFilter = vk::Filter::eLinear,
			.wrapU = vk::SamplerAddressMode::eClampToEdge,
			.wrapV = vk::SamplerAddressMode::eClampToEdge,
			.anisotropy = false,
			.gammaCorrection = true,
			.wrapW = vk::SamplerAddressMode::eClampToEdge,
		};
	}
};

struct TextureDescBindEntry
{
	std::shared_ptr<VKWrapper::BaseVKImage> image;
	std::shared_ptr<VKWrapper::VKImageView> imageView;
	std::shared_ptr<VKWrapper::VKSampler> sampler;
	uint32_t version;
};

namespace TextureGeneralDef
{
	float GetAnisotropicTextureFiltering();
}