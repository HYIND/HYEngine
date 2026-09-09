#include "vkstdafx.h"
#include "VulkanRenderEngine\VKWrapper\VKFrameBuffer.h"

using namespace VKWrapper;

VKFrameBuffer::~VKFrameBuffer()
{
	Release();
}

VKFrameBuffer::VKFrameBuffer(FrameBufferConfig& config)
{
	Create(config);
}

VKFrameBuffer::VKFrameBuffer(VKFrameBuffer&& other) noexcept
{
	_device = other._device;
	_renderPass = other._renderPass;
	_handle = other._handle;

	other._device = nullptr;
	other._renderPass = nullptr;
	other._handle = VK_NULL_HANDLE;
}

VKFrameBuffer& VKFrameBuffer::operator=(VKFrameBuffer&& other) noexcept
{
	if (this != &other)
	{
		Release();

		_device = other._device;
		_renderPass = other._renderPass;
		_handle = other._handle;

		other._device = nullptr;
		other._renderPass = nullptr;
		other._handle = VK_NULL_HANDLE;
	}

	return *this;
}

vk::Result VKFrameBuffer::Create(FrameBufferConfig& config)
{
	Release();

	vk::FramebufferCreateInfo createInfo;
	createInfo.setRenderPass(config.renderPass->GetHandle())
		.setAttachments(config.attachments)
		.setWidth(config.size.x)
		.setHeight(config.size.y)
		.setLayers(config.layers);

	auto [result, framebuffer] = config.device->GetHandle().createFramebuffer(createInfo);
	if (result != vk::Result::eSuccess)
	{
		outStream << std::format("[ framebuffer ] ERROR\nFailed to create a framebuffer!\nError code: {}\n", to_string(result));
		return result;
	}

	_device = config.device;
	_renderPass = config.renderPass;
	_handle = framebuffer;
	return vk::Result::eSuccess;
}

void VKFrameBuffer::Release()
{
	if (_device && _handle == VK_NULL_HANDLE)
		vkDestroyFramebuffer(_device->GetHandle(), _handle, nullptr);
}

VKCore::VulkanDevice* VKFrameBuffer::GetDevice() const { return _device; }

VKRenderPass* VKFrameBuffer::GetRenderPass() const { return _renderPass; }

vk::Framebuffer VKFrameBuffer::GetHandle() const { return _handle; }