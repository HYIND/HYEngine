#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKImageView.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

namespace
{
	class RetireImageView :public VKWrapper::IVKResource
	{
	public:
		RetireImageView(VKCore::VulkanDevice* device, vk::ImageView handle)
			:device(device), handle(handle)
		{}
		virtual void Destroy() {
			if (device && handle)
				device->GetHandle().destroyImageView(handle);
		}

	public:
		VKCore::VulkanDevice* device = nullptr;
		vk::ImageView handle = VK_NULL_HANDLE;
	};
}

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
	if (this == &other)
		return *this;
	Release();
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
		VKCONTEXT->Retire(new RetireImageView(_device, _handle));

	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

vk::ImageView VKWrapper::VKImageView::GetHandle() const { return _handle; }
