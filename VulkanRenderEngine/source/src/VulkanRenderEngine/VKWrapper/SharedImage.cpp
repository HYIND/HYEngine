#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/SharedImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

#include <dxgiformat.h>
#include <vulkan/vulkan.hpp>

static vk::Format DXGIFormatToVkFormat(DXGI_FORMAT fmt)
{
	switch (fmt)
	{
		// ---- 8-bit ----
	case DXGI_FORMAT_R8_UNORM:              return vk::Format::eR8Unorm;
	case DXGI_FORMAT_R8_SNORM:              return vk::Format::eR8Snorm;
	case DXGI_FORMAT_R8_UINT:               return vk::Format::eR8Uint;
	case DXGI_FORMAT_R8_SINT:               return vk::Format::eR8Sint;

		// ---- 16-bit ----
	case DXGI_FORMAT_R16_UNORM:             return vk::Format::eR16Unorm;
	case DXGI_FORMAT_R16_SNORM:             return vk::Format::eR16Snorm;
	case DXGI_FORMAT_R16_UINT:              return vk::Format::eR16Uint;
	case DXGI_FORMAT_R16_SINT:              return vk::Format::eR16Sint;
	case DXGI_FORMAT_R16_FLOAT:             return vk::Format::eR16Sfloat;

		// ---- 32-bit ----
	case DXGI_FORMAT_R32_UINT:              return vk::Format::eR32Uint;
	case DXGI_FORMAT_R32_SINT:              return vk::Format::eR32Sint;
	case DXGI_FORMAT_R32_FLOAT:             return vk::Format::eR32Sfloat;

		// ---- 2-channel ----
	case DXGI_FORMAT_R8G8_UNORM:            return vk::Format::eR8G8Unorm;
	case DXGI_FORMAT_R8G8_SNORM:            return vk::Format::eR8G8Snorm;
	case DXGI_FORMAT_R8G8_UINT:             return vk::Format::eR8G8Uint;
	case DXGI_FORMAT_R8G8_SINT:             return vk::Format::eR8G8Sint;

	case DXGI_FORMAT_R16G16_UNORM:          return vk::Format::eR16G16Unorm;
	case DXGI_FORMAT_R16G16_SNORM:          return vk::Format::eR16G16Snorm;
	case DXGI_FORMAT_R16G16_UINT:           return vk::Format::eR16G16Uint;
	case DXGI_FORMAT_R16G16_SINT:           return vk::Format::eR16G16Sint;
	case DXGI_FORMAT_R16G16_FLOAT:          return vk::Format::eR16G16Sfloat;

	case DXGI_FORMAT_R32G32_UINT:           return vk::Format::eR32G32Uint;
	case DXGI_FORMAT_R32G32_SINT:           return vk::Format::eR32G32Sint;
	case DXGI_FORMAT_R32G32_FLOAT:          return vk::Format::eR32G32Sfloat;

		// ---- 3-channel ----
	case DXGI_FORMAT_R32G32B32_UINT:        return vk::Format::eR32G32B32Uint;
	case DXGI_FORMAT_R32G32B32_SINT:        return vk::Format::eR32G32B32Sint;
	case DXGI_FORMAT_R32G32B32_FLOAT:       return vk::Format::eR32G32B32Sfloat;

		// ---- 4-channel 8-bit ----
	case DXGI_FORMAT_R8G8B8A8_UNORM:        return vk::Format::eR8G8B8A8Unorm;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return vk::Format::eR8G8B8A8Srgb;
	case DXGI_FORMAT_R8G8B8A8_SNORM:        return vk::Format::eR8G8B8A8Snorm;
	case DXGI_FORMAT_R8G8B8A8_UINT:         return vk::Format::eR8G8B8A8Uint;
	case DXGI_FORMAT_R8G8B8A8_SINT:         return vk::Format::eR8G8B8A8Sint;

	case DXGI_FORMAT_B8G8R8A8_UNORM:        return vk::Format::eB8G8R8A8Unorm;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return vk::Format::eB8G8R8A8Srgb;

		// ---- 4-channel 16-bit ----
	case DXGI_FORMAT_R16G16B16A16_UNORM:    return vk::Format::eR16G16B16A16Unorm;
	case DXGI_FORMAT_R16G16B16A16_SNORM:    return vk::Format::eR16G16B16A16Snorm;
	case DXGI_FORMAT_R16G16B16A16_UINT:     return vk::Format::eR16G16B16A16Uint;
	case DXGI_FORMAT_R16G16B16A16_SINT:     return vk::Format::eR16G16B16A16Sint;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:    return vk::Format::eR16G16B16A16Sfloat;

		// ---- 4-channel 32-bit ----
	case DXGI_FORMAT_R32G32B32A32_UINT:     return vk::Format::eR32G32B32A32Uint;
	case DXGI_FORMAT_R32G32B32A32_SINT:     return vk::Format::eR32G32B32A32Sint;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:    return vk::Format::eR32G32B32A32Sfloat;

		// ---- 10/11-bit packed ----
	case DXGI_FORMAT_R10G10B10A2_UNORM:     return vk::Format::eA2B10G10R10UnormPack32;
	case DXGI_FORMAT_R10G10B10A2_UINT:      return vk::Format::eA2B10G10R10UintPack32;
	case DXGI_FORMAT_R11G11B10_FLOAT:       return vk::Format::eB10G11R11UfloatPack32;

		// ---- depth/stencil（顺带）----
	case DXGI_FORMAT_D16_UNORM:             return vk::Format::eD16Unorm;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:     return vk::Format::eD24UnormS8Uint;
	case DXGI_FORMAT_D32_FLOAT:             return vk::Format::eD32Sfloat;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return vk::Format::eD32SfloatS8Uint;

	default:                                return vk::Format::eUndefined;
	}
}

static DXGI_FORMAT VkFormatToDXGIFormat(vk::Format fmt)
{
	switch (fmt)
	{
	case vk::Format::eR8Unorm:              return DXGI_FORMAT_R8_UNORM;
	case vk::Format::eR8Snorm:              return DXGI_FORMAT_R8_SNORM;
	case vk::Format::eR8Uint:               return DXGI_FORMAT_R8_UINT;
	case vk::Format::eR8Sint:               return DXGI_FORMAT_R8_SINT;

	case vk::Format::eR16Unorm:             return DXGI_FORMAT_R16_UNORM;
	case vk::Format::eR16Snorm:             return DXGI_FORMAT_R16_SNORM;
	case vk::Format::eR16Uint:              return DXGI_FORMAT_R16_UINT;
	case vk::Format::eR16Sint:              return DXGI_FORMAT_R16_SINT;
	case vk::Format::eR16Sfloat:            return DXGI_FORMAT_R16_FLOAT;

	case vk::Format::eR32Uint:              return DXGI_FORMAT_R32_UINT;
	case vk::Format::eR32Sint:              return DXGI_FORMAT_R32_SINT;
	case vk::Format::eR32Sfloat:            return DXGI_FORMAT_R32_FLOAT;

	case vk::Format::eR8G8Unorm:            return DXGI_FORMAT_R8G8_UNORM;
	case vk::Format::eR8G8Snorm:            return DXGI_FORMAT_R8G8_SNORM;
	case vk::Format::eR8G8Uint:             return DXGI_FORMAT_R8G8_UINT;
	case vk::Format::eR8G8Sint:             return DXGI_FORMAT_R8G8_SINT;

	case vk::Format::eR16G16Unorm:          return DXGI_FORMAT_R16G16_UNORM;
	case vk::Format::eR16G16Snorm:          return DXGI_FORMAT_R16G16_SNORM;
	case vk::Format::eR16G16Uint:           return DXGI_FORMAT_R16G16_UINT;
	case vk::Format::eR16G16Sint:           return DXGI_FORMAT_R16G16_SINT;
	case vk::Format::eR16G16Sfloat:         return DXGI_FORMAT_R16G16_FLOAT;

	case vk::Format::eR32G32Uint:           return DXGI_FORMAT_R32G32_UINT;
	case vk::Format::eR32G32Sint:           return DXGI_FORMAT_R32G32_SINT;
	case vk::Format::eR32G32Sfloat:         return DXGI_FORMAT_R32G32_FLOAT;

	case vk::Format::eR32G32B32Uint:        return DXGI_FORMAT_R32G32B32_UINT;
	case vk::Format::eR32G32B32Sint:        return DXGI_FORMAT_R32G32B32_SINT;
	case vk::Format::eR32G32B32Sfloat:      return DXGI_FORMAT_R32G32B32_FLOAT;

	case vk::Format::eR8G8B8A8Unorm:        return DXGI_FORMAT_R8G8B8A8_UNORM;
	case vk::Format::eR8G8B8A8Srgb:         return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case vk::Format::eR8G8B8A8Snorm:        return DXGI_FORMAT_R8G8B8A8_SNORM;
	case vk::Format::eR8G8B8A8Uint:         return DXGI_FORMAT_R8G8B8A8_UINT;
	case vk::Format::eR8G8B8A8Sint:         return DXGI_FORMAT_R8G8B8A8_SINT;

	case vk::Format::eB8G8R8A8Unorm:        return DXGI_FORMAT_B8G8R8A8_UNORM;
	case vk::Format::eB8G8R8A8Srgb:         return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

	case vk::Format::eR16G16B16A16Unorm:    return DXGI_FORMAT_R16G16B16A16_UNORM;
	case vk::Format::eR16G16B16A16Snorm:    return DXGI_FORMAT_R16G16B16A16_SNORM;
	case vk::Format::eR16G16B16A16Uint:     return DXGI_FORMAT_R16G16B16A16_UINT;
	case vk::Format::eR16G16B16A16Sint:     return DXGI_FORMAT_R16G16B16A16_SINT;
	case vk::Format::eR16G16B16A16Sfloat:   return DXGI_FORMAT_R16G16B16A16_FLOAT;

	case vk::Format::eR32G32B32A32Uint:     return DXGI_FORMAT_R32G32B32A32_UINT;
	case vk::Format::eR32G32B32A32Sint:     return DXGI_FORMAT_R32G32B32A32_SINT;
	case vk::Format::eR32G32B32A32Sfloat:   return DXGI_FORMAT_R32G32B32A32_FLOAT;

	case vk::Format::eA2B10G10R10UnormPack32: return DXGI_FORMAT_R10G10B10A2_UNORM;
	case vk::Format::eA2B10G10R10UintPack32:  return DXGI_FORMAT_R10G10B10A2_UINT;
	case vk::Format::eB10G11R11UfloatPack32:  return DXGI_FORMAT_R11G11B10_FLOAT;

	case vk::Format::eD16Unorm:             return DXGI_FORMAT_D16_UNORM;
	case vk::Format::eD24UnormS8Uint:       return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case vk::Format::eD32Sfloat:            return DXGI_FORMAT_D32_FLOAT;
	case vk::Format::eD32SfloatS8Uint:      return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

	default:                                return DXGI_FORMAT_UNKNOWN;
	}
}

uint32_t FindMemoryType(VKCore::VulkanDevice* device, uint32_t typeBits, vk::MemoryPropertyFlags properties)
{
	auto memProperties = device->GetPhysicalDevice().getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeBits & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	return UINT32_MAX;  // 没有找到合适的类型
}

SharedImage::~SharedImage() { Release(); }

bool VKWrapper::SharedImage::Create(VKCore::VulkanDevice* device, std::shared_ptr<SharedTexture> sharedTexture, vk::Format& outFormat)
{
	Release();

	uint32_t width = sharedTexture->width;
	uint32_t height = sharedTexture->height;

	vk::Format format = DXGIFormatToVkFormat(sharedTexture->format);

	// ============================================================
	// 1. 创建 VkImage（支持外部内存）
	// ============================================================
	vk::ExternalMemoryImageCreateInfo externalInfo;
	externalInfo.setHandleTypes(vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture);

	vk::ImageCreateInfo imageInfo;
	imageInfo.setImageType(vk::ImageType::e2D)
		.setFormat(format)
		.setExtent(vk::Extent3D(width, height, 1))
		.setMipLevels(1)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setPNext(&externalInfo);

	vk::Image vkImage;
	vk::Result result = device->GetHandle().createImage(&imageInfo, nullptr, &vkImage);
	if (result != vk::Result::eSuccess) {
		return false;
	}

	// ============================================================
	// 2. 查询句柄兼容的内存类型（关键步骤）
	// ============================================================
	auto [propresult, handleProps] = device->GetHandle().getMemoryWin32HandlePropertiesKHR(
		vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture,
		sharedTexture->sharedHandle
	);
	if (propresult != vk::Result::eSuccess) {
		device->GetHandle().destroyImage(vkImage);
		return false;
	}

	// 从 handleProps 中查找内存类型
	uint32_t memoryTypeIndex = FindMemoryType(
		device,
		handleProps.memoryTypeBits,  // 使用句柄提供的兼容类型位
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	if (memoryTypeIndex == UINT32_MAX) {
		// 如果没有 DeviceLocal，尝试不要求任何属性
		memoryTypeIndex = FindMemoryType(
			device,
			handleProps.memoryTypeBits,
			vk::MemoryPropertyFlags()
		);
	}
	if (memoryTypeIndex == UINT32_MAX) {
		device->GetHandle().destroyImage(vkImage);
		return false;
	}

	// ============================================================
	// 3. 获取图像内存需求
	// ============================================================
	vk::MemoryRequirements memRequirements;
	device->GetHandle().getImageMemoryRequirements(vkImage, &memRequirements);


	// ============================================================
	// 4. 导入内存（使用查询到的 memoryTypeIndex）
	// ============================================================
	vk::ImportMemoryWin32HandleInfoKHR importInfo;
	importInfo
		.setHandleType(vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture)
		.setHandle(sharedTexture->sharedHandle);

	vk::MemoryDedicatedAllocateInfo dedicatedInfo;
	dedicatedInfo.setImage(vkImage);
	dedicatedInfo.setPNext(&importInfo);

	// 填写内存分配信息
	vk::MemoryAllocateInfo allocInfo;
	allocInfo.setAllocationSize(memRequirements.size)
		.setMemoryTypeIndex(memoryTypeIndex)
		.setPNext(&dedicatedInfo);

	// 分配并导入内存
	vk::DeviceMemory importedMemory;
	result = device->GetHandle().allocateMemory(&allocInfo, nullptr, &importedMemory);
	if (result != vk::Result::eSuccess) {
		device->GetHandle().destroyImage(vkImage);
		return false;
	}

	// ============================================================
	// 5. 绑定图像和内存
	// ============================================================
	result = device->GetHandle().bindImageMemory(vkImage, importedMemory, 0);
	if (result != vk::Result::eSuccess) {
		device->GetHandle().freeMemory(importedMemory);
		device->GetHandle().destroyImage(vkImage);
		return false;
	}

	m_devicememory = importedMemory;
	m_image = vkImage;
	m_device = device;
	m_mipLevels = imageInfo.mipLevels;
	m_extent = imageInfo.extent;
	m_format = imageInfo.format;

	m_aspectMask = GetAspectMask(m_format);
	for (uint32_t i = 0; i < m_mipLevels; i++)
	{
		auto state = GetSubresourceState(i);
		state.layout = imageInfo.initialLayout;
		state.accessMask = vk::AccessFlags::BitsType::eNone;
	}

	outFormat = m_format;
	return true;
}

void SharedImage::Release()
{
	if (m_devicememory)
		m_device->GetHandle().freeMemory(m_devicememory);
	if (m_image)
		m_device->GetHandle().destroyImage(m_image);

	m_device = nullptr;
	m_image = VK_NULL_HANDLE;

	m_aspectMask = vk::ImageAspectFlagBits::eColor;
	_subresourceStates.clear();

	m_mipLevels = 1;
	m_extent = vk::Extent3D();
	m_format = vk::Format::eUndefined;
}
