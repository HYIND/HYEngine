#include "vkstdafx.h"
#include "VulkanRenderEngine\VKWrapper\VKSemaphore.h"
#include "VulkanRenderEngine\VKWrapper\VKResource.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;


class RestireSemaphore :public IVKResource
{
public:
	RestireSemaphore(VKCore::VulkanDevice* device, vk::Semaphore handle)
		:_device(device), _handle(handle)
	{}
	virtual void Destroy() {
		_device->GetHandle().destroySemaphore(_handle);
	}

public:
	VKCore::VulkanDevice* _device = nullptr;
	vk::Semaphore _handle = VK_NULL_HANDLE;
};


VKSemaphore::VKSemaphore(VKCore::VulkanDevice* device, const vk::SemaphoreCreateInfo& createInfo)
{
	Create(device, createInfo);
}

VKSemaphore::VKSemaphore(VKSemaphore&& other) noexcept {
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;
}

VKSemaphore& VKSemaphore::operator=(VKSemaphore&& other) noexcept
{
	if (this == &other)
		return *this;

	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;

	return *this;
}

VKSemaphore::~VKSemaphore() { Release(); }

vk::Result VKSemaphore::Create(VKCore::VulkanDevice* device, const vk::SemaphoreCreateInfo& createInfo) {
	Release();

	auto [result, handle] = device->GetHandle().createSemaphore(createInfo);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ VKSemaphore ] ERROR\nFailed to create a VKSemaphore!\nError code: {}\n", to_string(result));
	else
		_device = device;
	_handle = handle;
	return result;
}

void VKSemaphore::Release() {
	if (_device && _handle != VK_NULL_HANDLE)
		VKCONTEXT->Retire(new RestireSemaphore(_device, _handle));
	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

vk::Semaphore VKWrapper::VKSemaphore::GetHandle() const { return _handle; }
