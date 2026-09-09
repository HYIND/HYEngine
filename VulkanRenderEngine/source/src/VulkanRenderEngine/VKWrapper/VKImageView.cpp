#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKImageView.h"

using namespace VKWrapper;

VKWrapper::VKImageView::VKImageView(VKCore::VulkanDevice* device, const vk::ImageViewCreateInfo& createInfo)
{
	Create(device, createInfo);
}

VKWrapper::VKImageView::VKImageView(VKImageView&& other) noexcept
{
	*this = std::move(other);
}

VKImageView& VKWrapper::VKImageView::operator=(VKImageView&& other) noexcept
{
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = VK_NULL_HANDLE;
	return *this;
}

VKWrapper::VKImageView::~VKImageView()
{
	Release();
}

vk::Result VKWrapper::VKImageView::Create(VKCore::VulkanDevice* device, const vk::ImageViewCreateInfo& createInfo)
{
	if (!device)
		return vk::Result::eErrorInitializationFailed;

	Release();

	auto [result, imageView] = device->GetHandle().createImageView(createInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("fail to create imageview!\n");
		return result;
	}

	_device = device;
	_handle = imageView;

	return vk::Result::eSuccess;
}

void VKWrapper::VKImageView::Release()
{
	if (_device && _handle)
		_device->GetHandle().destroyImageView(_handle);

	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

vk::ImageView VKWrapper::VKImageView::GetHandle() const { return _handle; }
