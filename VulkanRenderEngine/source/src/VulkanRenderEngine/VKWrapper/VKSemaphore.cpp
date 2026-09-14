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


vk::Semaphore VKWrapper::VKSemaphore::GetHandle() const { return _handle; }

bool VKWrapper::VKSemaphore::IsTimeline() const { return _isTimeline; }

void VKWrapper::VKSemaphore::Release()
{
	if (_device && _handle != VK_NULL_HANDLE)
		VKCONTEXT->Retire(new RestireSemaphore(_device, _handle));
	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

VKBinarySemaphore::VKBinarySemaphore(VKCore::VulkanDevice* device)
{
	_isTimeline = false;
	Create(device);
}

VKBinarySemaphore::VKBinarySemaphore(VKBinarySemaphore&& other) noexcept {
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;
}

VKBinarySemaphore& VKBinarySemaphore::operator=(VKBinarySemaphore&& other) noexcept
{
	if (this == &other)
		return *this;

	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;

	return *this;
}

VKBinarySemaphore::~VKBinarySemaphore() { Release(); }

vk::Result VKBinarySemaphore::Create(VKCore::VulkanDevice* device) {
	Release();

	vk::SemaphoreCreateInfo createInfo;
	auto [result, handle] = device->GetHandle().createSemaphore(createInfo);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ VKBinarySemaphore ] ERROR\nFailed to create a VKBinarySemaphore!\nError code: {}\n", to_string(result));
	else
		_device = device;
	_handle = handle;
	return result;
}

VKTimelineSemaphore::VKTimelineSemaphore(VKCore::VulkanDevice* device, const uint64_t initialValue)
{
	_isTimeline = true;
	Create(device, initialValue);
}

VKTimelineSemaphore::VKTimelineSemaphore(VKTimelineSemaphore&& other) noexcept {
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;
}

VKTimelineSemaphore& VKTimelineSemaphore::operator=(VKTimelineSemaphore&& other) noexcept
{
	if (this == &other)
		return *this;

	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;

	return *this;
}

VKTimelineSemaphore::~VKTimelineSemaphore() { Release(); }

vk::Result VKTimelineSemaphore::Create(VKCore::VulkanDevice* device, const uint64_t initialValue) {
	Release();

	vk::SemaphoreTypeCreateInfo typeInfo;
	typeInfo
		.setSemaphoreType(vk::SemaphoreType::eTimeline)
		.setInitialValue(0);

	vk::SemaphoreCreateInfo createInfo;
	createInfo.setPNext(&typeInfo);

	auto [result, handle] = device->GetHandle().createSemaphore(createInfo);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ VKTimelineSemaphore ] ERROR\nFailed to create a VKTimelineSemaphore!\nError code: {}\n", to_string(result));
	else
		_device = device;
	_handle = handle;
	return result;
}
