#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/SharedImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

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

	//vk::Format format = vk::Format::eR8G8B8A8Unorm;
	vk::Format format = vk::Format::eB8G8R8A8Unorm;

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
