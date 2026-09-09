#pragma once

#include "stdafx.h"
#include "vulkan/vk_enum_string_helper.h"

// ==================== 全局变量 ====================
extern VkAllocationCallbacks*	g_Allocator;
extern VkInstance               g_Instance;
extern VkPhysicalDevice         g_PhysicalDevice;
extern VkDevice                 g_Device;
extern uint32_t                 g_QueueFamily;
extern VkQueue                  g_Queue;
extern VkPipelineCache          g_PipelineCache;
extern VkDescriptorPool         g_DescriptorPool;

extern ImGui_ImplVulkanH_Window g_MainWindowData;
extern uint32_t                 g_MinImageCount;
extern bool                     g_SwapChainRebuild;

// ==================== Vulkan 错误处理 ====================
void check_vk_result(VkResult err);

// ==================== Vulkan 初始化 ====================
bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension);
void SetupVulkan(ImVector<const char*> instance_extensions);
void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height);

// ==================== 清理函数 ====================
void CleanupVulkan();
void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd);

// ==================== 渲染函数 ====================
void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data);
void FramePresent(ImGui_ImplVulkanH_Window* wd);


// =================== 外部导入 =====================
void SetupVulkan(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkDescriptorPool descriptorPool);
