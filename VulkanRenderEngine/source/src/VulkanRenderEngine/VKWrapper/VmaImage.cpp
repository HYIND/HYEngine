#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaImage.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

class RestireImage :public IVKResource
{
public:
	RestireImage(VmaAllocator allocator, vk::Image image, VmaAllocation allocation)
		:m_allocator(allocator), m_image(image), m_allocation(allocation)
	{}
	virtual void Destroy() {
		vmaDestroyImage(m_allocator, m_image, m_allocation);
	}

public:
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	vk::Image m_image = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
};

void static PrintFormatSupport(vk::PhysicalDevice physicalDevice, vk::Format format)
{
	vk::FormatProperties2 formatProps{};
	formatProps.sType = vk::StructureType::eFormatProperties2;

	physicalDevice.getFormatProperties2(format, &formatProps);

	vk::FormatFeatureFlags features = formatProps.formatProperties.optimalTilingFeatures;

	std::cout << "========================================" << std::endl;
	std::cout << "格式: " << vk::to_string(format) << std::endl;
	std::cout << "========================================" << std::endl;

	// 1. 纹理采样
	std::cout << "eSampledImage              : " << ((features & vk::FormatFeatureFlagBits::eSampledImage) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 2. 存储图像
	std::cout << "eStorageImage              : " << ((features & vk::FormatFeatureFlagBits::eStorageImage) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 3. 颜色渲染目标
	std::cout << "eColorAttachment           : " << ((features & vk::FormatFeatureFlagBits::eColorAttachment) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 4. 颜色渲染目标混合
	std::cout << "eColorAttachmentBlend      : " << ((features & vk::FormatFeatureFlagBits::eColorAttachmentBlend) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 5. 深度/模板附件
	std::cout << "eDepthStencilAttachment    : " << ((features & vk::FormatFeatureFlagBits::eDepthStencilAttachment) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 6. 传输源
	std::cout << "eTransferSrc               : " << ((features & vk::FormatFeatureFlagBits::eTransferSrc) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 7. 传输目标
	std::cout << "eTransferDst               : " << ((features & vk::FormatFeatureFlagBits::eTransferDst) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 8. 统一纹理缓冲视图
	std::cout << "eUniformTexelBuffer        : " << ((features & vk::FormatFeatureFlagBits::eUniformTexelBuffer) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 9. 存储纹理缓冲视图
	std::cout << "eStorageTexelBuffer        : " << ((features & vk::FormatFeatureFlagBits::eStorageTexelBuffer) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 10. 顶点缓冲区的格式
	std::cout << "eVertexBuffer              : " << ((features & vk::FormatFeatureFlagBits::eVertexBuffer) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 11. 线性过滤
	std::cout << "eSampledImageFilterLinear  : " << ((features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) ? "✅ 支持" : "❌ 不支持") << std::endl;

	// 12. 各向异性过滤
	std::cout << "eSampledImageFilterMinmax  : " << ((features & vk::FormatFeatureFlagBits::eSampledImageFilterMinmax) ? "✅ 支持" : "❌ 不支持") << std::endl;

	std::cout << "========================================" << std::endl;
	std::cout << std::endl;
}

VmaImage::~VmaImage() { Release(); }

bool VmaImage::Create(VKCore::VulkanDevice* device,
	const vk::ImageCreateInfo& imageInfo,
	const VmaAllocationCreateInfo& allocInfo) {

	Release();

	auto allocator = device->GetAllocator();
	VkResult result = vmaCreateImage(allocator, (VkImageCreateInfo*)(&imageInfo), &allocInfo, (VkImage*)&m_image, &m_allocation, nullptr);
	if (result != VK_SUCCESS)
		return false;

	m_device = device;
	m_allocator = allocator;
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

	return true;
}

bool VmaImage::Create(VKCore::VulkanDevice* device,
	vk::Format format,
	vk::Extent2D size,
	uint32_t mipLevels,
	ImageType type,
	bool cpuAccess
) {

	Release();

	static vk::ImageUsageFlags linearColorUsage =
		vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eStorage |
		vk::ImageUsageFlagBits::eColorAttachment;

	static vk::ImageUsageFlags colorUsage =
		vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eColorAttachment;

	static vk::ImageUsageFlags depthUsage =
		vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eStorage |
		vk::ImageUsageFlagBits::eDepthStencilAttachment;

	static vk::ImageUsageFlags depthStencilUsage =
		vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eDepthStencilAttachment;

	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR32G32B32A32Sfloat);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8B8A8Unorm);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8B8A8Srgb);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR32G32B32Sfloat);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8B8Unorm);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8B8Srgb);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR32G32Sfloat);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8Unorm);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8G8Srgb);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR32Sfloat);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8Unorm);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eR8Srgb);
	//PrintFormatSupport(device->GetPhysicalDevice(), vk::Format::eD32Sfloat);

	auto GetUsageFlags = [&]->vk::ImageUsageFlags {
		if (IsColorFormat(format))
			return IsLinearFormat(format) ? linearColorUsage : colorUsage;
		else if (IsDepthFormat(format))
			return depthUsage;
		else
			return depthStencilUsage;
		};

	vk::ImageUsageFlags usage = GetUsageFlags();

	bool isCubemap = (type == ImageType::ImageCube);

	vk::ImageCreateInfo imageInfo = {};
	imageInfo.
		setFlags(isCubemap ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlagBits(0))
		.setImageType(vk::ImageType::e2D)
		.setFormat(format)
		.setExtent({ size.width,size.height, 1 })
		.setMipLevels(std::max((uint32_t)1, mipLevels))
		.setArrayLayers(isCubemap ? 6 : 1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(vk::ImageLayout::eUndefined);

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	if (cpuAccess)
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	auto allocator = device->GetAllocator();
	VkResult result = vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, reinterpret_cast<VkImage*>(&m_image), &m_allocation, nullptr);
	if (result != VK_SUCCESS)
		return false;

	m_device = device;
	m_allocator = allocator;
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

	return true;
}


void VmaImage::Release() {
	if (m_allocator && m_image && m_allocation) {
		VKCONTEXT->Retire(new RestireImage(m_allocator, m_image, m_allocation));
	}
	m_device = nullptr;
	m_allocator = VK_NULL_HANDLE;
	m_image = VK_NULL_HANDLE;
	m_allocation = VK_NULL_HANDLE;

	m_aspectMask = vk::ImageAspectFlagBits::eColor;
	_subresourceStates.clear();

	m_mipLevels = 1;
	m_extent = vk::Extent3D();
	m_format = vk::Format::eUndefined;
}
