#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanSurface.h"

using namespace VKCore;

VulkanSurface::VulkanSurface(std::weak_ptr<VulkanInstance> instance, vk::SurfaceKHR handle)
	:m_instance(instance), m_surface(handle)
{
}

VulkanSurface::~VulkanSurface()
{
	Release();
}

void VulkanSurface::Release() {
	if (auto instance = m_instance.lock(); instance && m_surface)
		vkDestroySurfaceKHR(instance->GetHandle(), m_surface, nullptr);
	m_surface = VK_NULL_HANDLE;
}

// ---------- Getter ----------
vk::SurfaceKHR VulkanSurface::GetHandle() const { return m_surface; }

std::weak_ptr<VulkanInstance> VulkanSurface::GetInstance() const { return m_instance; }

bool VulkanSurface::IsValid() const { return m_surface != VK_NULL_HANDLE; }
