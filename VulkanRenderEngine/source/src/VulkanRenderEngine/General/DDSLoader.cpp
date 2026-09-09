#include "vkstdafx.h"
#include "VulkanRenderEngine/General/DDSLoader.h"


// DXGI 到 Vulkan 格式转换函数
VkFormat DXGIToVulkanFormat(DXGI_FORMAT dxgiFormat)
{
	switch (dxgiFormat)
	{
		// ==================== 非压缩格式 ====================

		// R8 系列
	case DXGI_FORMAT_R8_UNORM:              return VK_FORMAT_R8_UNORM;
	case DXGI_FORMAT_R8_SNORM:              return VK_FORMAT_R8_SNORM;
	case DXGI_FORMAT_R8_UINT:               return VK_FORMAT_R8_UINT;
	case DXGI_FORMAT_R8_SINT:               return VK_FORMAT_R8_SINT;

		// R8G8 系列
	case DXGI_FORMAT_R8G8_UNORM:            return VK_FORMAT_R8G8_UNORM;
	case DXGI_FORMAT_R8G8_SNORM:            return VK_FORMAT_R8G8_SNORM;
	case DXGI_FORMAT_R8G8_UINT:             return VK_FORMAT_R8G8_UINT;
	case DXGI_FORMAT_R8G8_SINT:             return VK_FORMAT_R8G8_SINT;

		// R8G8B8A8 系列
	case DXGI_FORMAT_R8G8B8A8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return VK_FORMAT_R8G8B8A8_SRGB;
	case DXGI_FORMAT_R8G8B8A8_UINT:         return VK_FORMAT_R8G8B8A8_UINT;
	case DXGI_FORMAT_R8G8B8A8_SINT:         return VK_FORMAT_R8G8B8A8_SINT;
	case DXGI_FORMAT_R8G8B8A8_SNORM:        return VK_FORMAT_R8G8B8A8_SNORM;

		// B8G8R8A8 系列
	case DXGI_FORMAT_B8G8R8A8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return VK_FORMAT_B8G8R8A8_SRGB;
	case DXGI_FORMAT_B8G8R8X8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return VK_FORMAT_B8G8R8A8_SRGB;

		// R16 系列
	case DXGI_FORMAT_R16_UNORM:             return VK_FORMAT_R16_UNORM;
	case DXGI_FORMAT_R16_SNORM:             return VK_FORMAT_R16_SNORM;
	case DXGI_FORMAT_R16_UINT:              return VK_FORMAT_R16_UINT;
	case DXGI_FORMAT_R16_SINT:              return VK_FORMAT_R16_SINT;
	case DXGI_FORMAT_R16_FLOAT:             return VK_FORMAT_R16_SFLOAT;

		// R16G16 系列
	case DXGI_FORMAT_R16G16_UNORM:          return VK_FORMAT_R16G16_UNORM;
	case DXGI_FORMAT_R16G16_SNORM:          return VK_FORMAT_R16G16_SNORM;
	case DXGI_FORMAT_R16G16_UINT:           return VK_FORMAT_R16G16_UINT;
	case DXGI_FORMAT_R16G16_SINT:           return VK_FORMAT_R16G16_SINT;
	case DXGI_FORMAT_R16G16_FLOAT:          return VK_FORMAT_R16G16_SFLOAT;

		// R16G16B16A16 系列
	case DXGI_FORMAT_R16G16B16A16_UNORM:    return VK_FORMAT_R16G16B16A16_UNORM;
	case DXGI_FORMAT_R16G16B16A16_SNORM:    return VK_FORMAT_R16G16B16A16_SNORM;
	case DXGI_FORMAT_R16G16B16A16_UINT:     return VK_FORMAT_R16G16B16A16_UINT;
	case DXGI_FORMAT_R16G16B16A16_SINT:     return VK_FORMAT_R16G16B16A16_SINT;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:    return VK_FORMAT_R16G16B16A16_SFLOAT;

		// R32 系列
	case DXGI_FORMAT_R32_UINT:              return VK_FORMAT_R32_UINT;
	case DXGI_FORMAT_R32_SINT:              return VK_FORMAT_R32_SINT;
	case DXGI_FORMAT_R32_FLOAT:             return VK_FORMAT_R32_SFLOAT;

		// R32G32 系列
	case DXGI_FORMAT_R32G32_UINT:           return VK_FORMAT_R32G32_UINT;
	case DXGI_FORMAT_R32G32_SINT:           return VK_FORMAT_R32G32_SINT;
	case DXGI_FORMAT_R32G32_FLOAT:          return VK_FORMAT_R32G32_SFLOAT;

		// R32G32B32 系列
	case DXGI_FORMAT_R32G32B32_UINT:        return VK_FORMAT_R32G32B32_UINT;
	case DXGI_FORMAT_R32G32B32_SINT:        return VK_FORMAT_R32G32B32_SINT;
	case DXGI_FORMAT_R32G32B32_FLOAT:       return VK_FORMAT_R32G32B32_SFLOAT;

		// R32G32B32A32 系列
	case DXGI_FORMAT_R32G32B32A32_UINT:     return VK_FORMAT_R32G32B32A32_UINT;
	case DXGI_FORMAT_R32G32B32A32_SINT:     return VK_FORMAT_R32G32B32A32_SINT;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:    return VK_FORMAT_R32G32B32A32_SFLOAT;

		// ==================== BC 压缩格式 ====================

		// BC1 (DXT1)
	case DXGI_FORMAT_BC1_UNORM:             return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case DXGI_FORMAT_BC1_UNORM_SRGB:        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;

		// BC2 (DXT3)
	case DXGI_FORMAT_BC2_UNORM:             return VK_FORMAT_BC2_UNORM_BLOCK;
	case DXGI_FORMAT_BC2_UNORM_SRGB:        return VK_FORMAT_BC2_SRGB_BLOCK;

		// BC3 (DXT5)
	case DXGI_FORMAT_BC3_UNORM:             return VK_FORMAT_BC3_UNORM_BLOCK;
	case DXGI_FORMAT_BC3_UNORM_SRGB:        return VK_FORMAT_BC3_SRGB_BLOCK;

		// BC4
	case DXGI_FORMAT_BC4_UNORM:             return VK_FORMAT_BC4_UNORM_BLOCK;
	case DXGI_FORMAT_BC4_SNORM:             return VK_FORMAT_BC4_SNORM_BLOCK;

		// BC5
	case DXGI_FORMAT_BC5_UNORM:             return VK_FORMAT_BC5_UNORM_BLOCK;
	case DXGI_FORMAT_BC5_SNORM:             return VK_FORMAT_BC5_SNORM_BLOCK;

		// BC6H
	case DXGI_FORMAT_BC6H_UF16:             return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case DXGI_FORMAT_BC6H_SF16:             return VK_FORMAT_BC6H_SFLOAT_BLOCK;

		// BC7
	case DXGI_FORMAT_BC7_UNORM:             return VK_FORMAT_BC7_UNORM_BLOCK;
	case DXGI_FORMAT_BC7_UNORM_SRGB:        return VK_FORMAT_BC7_SRGB_BLOCK;

		// ==================== 深度/模板格式 ====================

	case DXGI_FORMAT_D16_UNORM:             return VK_FORMAT_D16_UNORM;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:     return VK_FORMAT_D24_UNORM_S8_UINT;
	case DXGI_FORMAT_D32_FLOAT:             return VK_FORMAT_D32_SFLOAT;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return VK_FORMAT_D32_SFLOAT_S8_UINT;

		// ==================== 不支持的格式 ====================

		// Typeless 格式（需要进一步转换）
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
		return VK_FORMAT_UNDEFINED;

		// 旧格式（不推荐使用）
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
	case DXGI_FORMAT_A8P8:
		return VK_FORMAT_UNDEFINED;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

// 反向转换：Vulkan 到 DXGI（
DXGI_FORMAT VulkanToDXGIFormat(VkFormat vkFormat)
{
	switch (vkFormat)
	{
		// 非压缩格式
	case VK_FORMAT_R8_UNORM:                return DXGI_FORMAT_R8_UNORM;
	case VK_FORMAT_R8_SNORM:                return DXGI_FORMAT_R8_SNORM;
	case VK_FORMAT_R8_UINT:                 return DXGI_FORMAT_R8_UINT;
	case VK_FORMAT_R8_SINT:                 return DXGI_FORMAT_R8_SINT;

	case VK_FORMAT_R8G8_UNORM:              return DXGI_FORMAT_R8G8_UNORM;
	case VK_FORMAT_R8G8_SNORM:              return DXGI_FORMAT_R8G8_SNORM;
	case VK_FORMAT_R8G8_UINT:               return DXGI_FORMAT_R8G8_UINT;
	case VK_FORMAT_R8G8_SINT:               return DXGI_FORMAT_R8G8_SINT;

	case VK_FORMAT_R8G8B8A8_UNORM:          return DXGI_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB:           return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case VK_FORMAT_R8G8B8A8_UINT:           return DXGI_FORMAT_R8G8B8A8_UINT;
	case VK_FORMAT_R8G8B8A8_SINT:           return DXGI_FORMAT_R8G8B8A8_SINT;
	case VK_FORMAT_R8G8B8A8_SNORM:          return DXGI_FORMAT_R8G8B8A8_SNORM;

	case VK_FORMAT_B8G8R8A8_UNORM:          return DXGI_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_SRGB:           return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

		// BC 压缩格式
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:    return DXGI_FORMAT_BC1_UNORM;
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:     return DXGI_FORMAT_BC1_UNORM_SRGB;

	case VK_FORMAT_BC2_UNORM_BLOCK:         return DXGI_FORMAT_BC2_UNORM;
	case VK_FORMAT_BC2_SRGB_BLOCK:          return DXGI_FORMAT_BC2_UNORM_SRGB;

	case VK_FORMAT_BC3_UNORM_BLOCK:         return DXGI_FORMAT_BC3_UNORM;
	case VK_FORMAT_BC3_SRGB_BLOCK:          return DXGI_FORMAT_BC3_UNORM_SRGB;

	case VK_FORMAT_BC4_UNORM_BLOCK:         return DXGI_FORMAT_BC4_UNORM;
	case VK_FORMAT_BC4_SNORM_BLOCK:         return DXGI_FORMAT_BC4_SNORM;

	case VK_FORMAT_BC5_UNORM_BLOCK:         return DXGI_FORMAT_BC5_UNORM;
	case VK_FORMAT_BC5_SNORM_BLOCK:         return DXGI_FORMAT_BC5_SNORM;

	case VK_FORMAT_BC6H_UFLOAT_BLOCK:       return DXGI_FORMAT_BC6H_UF16;
	case VK_FORMAT_BC6H_SFLOAT_BLOCK:       return DXGI_FORMAT_BC6H_SF16;

	case VK_FORMAT_BC7_UNORM_BLOCK:         return DXGI_FORMAT_BC7_UNORM;
	case VK_FORMAT_BC7_SRGB_BLOCK:          return DXGI_FORMAT_BC7_UNORM_SRGB;

		// 深度格式
	case VK_FORMAT_D16_UNORM:               return DXGI_FORMAT_D16_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT:       return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_D32_SFLOAT:              return DXGI_FORMAT_D32_FLOAT;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:      return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

static void FillNormalZ(std::vector<uint8_t>& rgbaData)
{
	auto pixels = std::span<glm::u8vec4>(
		reinterpret_cast<glm::u8vec4*>(rgbaData.data()),
		rgbaData.size() / 4
	);

	std::for_each(std::execution::par_unseq, pixels.begin(), pixels.end(),
		[](glm::u8vec4& p) {
			glm::vec2 normal2 = glm::vec2(p.r, p.g) / 255.0f * 2.0f - 1.0f;
			float z = std::sqrt(std::max(0.0f, 1.0f - glm::dot(normal2, normal2)));
			p.b = static_cast<uint8_t>(z * 255.0f + 0.5f);
			p.a = 255;
		}
	);
}

DDSLoadResult LoadDDSFile(const std::string& filepath, vk::Format targetFormat)
{
	DDSLoadResult result;
	auto dxgi_TargetFormat = VulkanToDXGIFormat((VkFormat)targetFormat);

	try {
		const std::wstring w_filepath = Tool::UTF8ToWString(filepath);

		HRESULT hr = S_OK;

		// 加载DDS文件
		ScratchImage image;
		TexMetadata metadata;
		hr = LoadFromDDSFile(w_filepath.c_str(), DDS_FLAGS_NONE, &metadata, image);
		if (FAILED(hr))
		{
			std::cout << std::format("DDSLoader Load File Failed! hr = {}, path = {}\n", hr, filepath);
			return {};
		}

		bool needConvert = metadata.format != dxgi_TargetFormat;

		ScratchImage convertedImage;
		const Image* img = nullptr;
		if (needConvert) {
			hr = Decompress(*image.GetImages(), dxgi_TargetFormat, convertedImage);

			if (FAILED(hr)) {
				std::cout << std::format("DDSLoader Decompress Failed! hr = 0x{:08X}, path = {}\n", hr, filepath);
				return {};
			}

			img = convertedImage.GetImage(0, 0, 0);
		}
		else {
			img = image.GetImage(0, 0, 0);
		}

		if (!img) {
			std::cout << std::format("DDSLoader Get Image Data Failed! path = {}\n", filepath);
			return {};
		}

		result.width = metadata.width;
		result.height = metadata.height;
		result.format = targetFormat;

		//ScratchImage filpYOutPut;
		//hr = FlipRotate(*img, DirectX::TEX_FR_FLIP_HORIZONTAL, filpYOutPut);
		//if (FAILED(hr)) {
		//	std::cout << std::format("DDSLoader FilpY Failed! hr = 0x{:08X}, path = {}\n", hr, filepath);
		//	return {};
		//}

		//img = filpYOutPut.GetImage(0, 0, 0);

		size_t dataSize = img->slicePitch;
		result.data.resize(dataSize);
		memcpy(result.data.data(), img->pixels, dataSize);

		if (metadata.format == DXGI_FORMAT_BC5_UNORM && targetFormat == vk::Format::eR8G8B8A8Unorm)
			FillNormalZ(result.data);

		result.valid = true;
	}
	catch (const std::exception& e) {
		std::cout << std::format("DDSLoader Exception: {}\n", e.what());
	}

	return result;
}
