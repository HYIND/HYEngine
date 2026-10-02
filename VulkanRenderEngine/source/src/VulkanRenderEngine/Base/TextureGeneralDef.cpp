#include "vkstdafx.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/TextureGeneralDef.h"

using namespace TextureGeneralDef;

bool TextureConfig::operator==(const TextureConfig& other) const
{
	return minFilter == other.minFilter
		&& magFilter == other.magFilter
		&& wrapU == other.wrapU
		&& wrapV == other.wrapV
		&& wrapW == other.wrapW
		&& anisotropy == other.anisotropy
		&& gammaCorrection == other.gammaCorrection;
}

bool TextureConfig::operator!=(const TextureConfig& other) const
{
	return !(*this == other);
}

float TextureGeneralDef::GetAnisotropicTextureFiltering()
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
