#pragma once
#include "vkstdafx.h"
#include "VulkanInstance.h"

namespace VKCore
{

	class VulkanSurface
	{
	public:
		VulkanSurface() = default;
		VulkanSurface(std::weak_ptr<VulkanInstance> instance, vk::SurfaceKHR handle);
		~VulkanSurface();

		// 禁止拷贝，允许移动
		VulkanSurface(const VulkanSurface&) = delete;
		VulkanSurface& operator=(const VulkanSurface&) = delete;
		VulkanSurface(VulkanSurface&& other) noexcept;
		VulkanSurface& operator=(VulkanSurface&& other) noexcept;

		void Release();

	public:
		// ---------- Getter ----------
		vk::SurfaceKHR GetHandle() const;
		std::weak_ptr<VulkanInstance> GetInstance() const;
		bool IsValid() const;

	private:
		std::weak_ptr<VulkanInstance> m_instance;
		vk::SurfaceKHR m_surface = VK_NULL_HANDLE;
	};
}