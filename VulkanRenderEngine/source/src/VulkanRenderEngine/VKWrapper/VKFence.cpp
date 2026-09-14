#include "vkstdafx.h"
#include "VulkanRenderEngine\VKWrapper\VKFence.h"
#include "VulkanRenderEngine\VKWrapper\VKResource.h"
#include "VulkanRenderEngine/VKContext.h"

using namespace VKWrapper;

class RestireFence :public IVKResource
{
public:
	RestireFence(VKCore::VulkanDevice* device, vk::Fence handle)
		:_device(device), _handle(handle)
	{}
	virtual void Destroy() {
		_device->GetHandle().destroyFence(_handle);
	}

public:
	VKCore::VulkanDevice* _device = nullptr;
	vk::Fence _handle = VK_NULL_HANDLE;
};


VKFence::VKFence(VKCore::VulkanDevice* device, const vk::FenceCreateInfo& createInfo)
{
	Create(device, createInfo);
}

VKFence::VKFence(VKFence&& other) noexcept {
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;
}

VKFence& VKFence::operator=(VKFence&& other) noexcept
{
	if (this == &other)
		return *this;

	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = nullptr;

	return *this;
}

VKFence::~VKFence() { Release(); }

vk::Result VKFence::Create(VKCore::VulkanDevice* device, const vk::FenceCreateInfo& createInfo)
{
	if (!device)
		return vk::Result::eErrorInitializationFailed;

	Release();
	auto [result, fence] = device->GetHandle().createFence(createInfo);
	if (result != vk::Result::eSuccess)
	{
		outStream << std::format("[ fence ] ERROR\nFailed to create a fence!\nError code: {}\n", to_string(result));
		return result;
	}

	_device = device;
	_handle = fence;
	return vk::Result::eSuccess;
}

void VKFence::Release() {
	if (_device && _handle != VK_NULL_HANDLE)
		VKCONTEXT->Retire(new RestireFence(_device, _handle));
	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

vk::Fence VKWrapper::VKFence::GetHandle() const { return _handle; }

vk::Result VKFence::Wait() const {
	auto result = _device->GetHandle().waitForFences(_handle, true, UINT64_MAX);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ fence ] ERROR\nFailed to wait for the fence!\nError code: {}\n", to_string(result));
	return result;
}

vk::Result VKFence::Reset() const {
	auto result = _device->GetHandle().resetFences(_handle);
	if (result != vk::Result::eSuccess)
		outStream << std::format("[ fence ] ERROR\nFailed to reset the fence!\nError code: {}\n", to_string(result));
	return result;
}

vk::Result VKFence::WaitAndReset() const
{
	if (auto result = Wait(); result != vk::Result::eSuccess)
		return result;
	if (auto result = Reset(); result != vk::Result::eSuccess)
		return result;
	return vk::Result::eSuccess;
}

vk::Result VKFence::Status() const {
	auto result = _device->GetHandle().getFenceStatus(_handle);
	if (result != vk::Result::eSuccess && result != vk::Result::eNotReady)
		outStream << std::format("[ fence ] ERROR\nFailed to get the status of the fence!\nError code: {}\n", to_string(result));
	return result;
}