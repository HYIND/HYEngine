#pragma once
#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKSampler.h"

using namespace VKWrapper;

VKWrapper::VKSampler::VKSampler(VKCore::VulkanDevice* device, const vk::SamplerCreateInfo& createInfo)
{
	Create(device, createInfo);
}

VKWrapper::VKSampler::VKSampler(VKSampler&& other) noexcept
{
	*this = std::move(other);
}

VKSampler& VKWrapper::VKSampler::operator=(VKSampler&& other) noexcept
{
	_device = other._device;
	_handle = other._handle;
	other._device = nullptr;
	other._handle = VK_NULL_HANDLE;
	return *this;
}

VKWrapper::VKSampler::~VKSampler()
{
	Release();
}

vk::Result VKWrapper::VKSampler::Create(VKCore::VulkanDevice* device, const vk::SamplerCreateInfo& createInfo)
{
	if (!device)
		return vk::Result::eErrorInitializationFailed;

	Release();

	auto [result, sampler] = device->GetHandle().createSampler(createInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("fail to create sampler!\n");
		return result;
	}

	_device = device;
	_handle = sampler;

	return vk::Result::eSuccess;
}

void VKWrapper::VKSampler::Release()
{
	if (_device && _handle)
		_device->GetHandle().destroySampler(_handle);

	_device = nullptr;
	_handle = VK_NULL_HANDLE;
}

vk::Sampler VKWrapper::VKSampler::GetHandle() const { return _handle; }
