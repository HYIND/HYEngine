
#include "ImguiInit/VulkanInit.h"

// ==================== 全局变量 ====================
VkAllocationCallbacks*   g_Allocator = nullptr;
VkInstance               g_Instance = VK_NULL_HANDLE;
VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
VkDevice                 g_Device = VK_NULL_HANDLE;
uint32_t                 g_QueueFamily = (uint32_t)-1;
VkQueue                  g_Queue = VK_NULL_HANDLE;
VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

ImGui_ImplVulkanH_Window g_MainWindowData;
uint32_t                 g_MinImageCount = 2;
bool                     g_SwapChainRebuild = false;

void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

// ==================== Vulkan 初始化 ====================
bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

void SetupVulkan(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkDescriptorPool descriptorPool)  // 新增参数
{
    g_Instance = instance;
    g_PhysicalDevice = physicalDevice;
    g_Device = device;
    g_QueueFamily = queueFamily;
    g_Queue = queue;
    g_DescriptorPool = descriptorPool;  // 使用外部传入的

}

void SetupVulkan(ImVector<const char*> instance_extensions)
{
    VkResult err;

    // 1. 创建 Vulkan 实例
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
        create_info.ppEnabledExtensionNames = instance_extensions.Data;
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
    }

    // 2. 选择物理设备
    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

    // 3. 选择队列族
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    IM_ASSERT(g_QueueFamily != (uint32_t)-1);

    // 4. 创建逻辑设备
    {
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = g_QueueFamily;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = queue_priority;

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;

        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        check_vk_result(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // 5. 创建描述符池
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
            { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;
        pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
    }
}

void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) {
    // 检查 WSI 支持
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error: no WSI support on physical device\n");
        exit(-1);
    }

    // 选择表面格式
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        g_PhysicalDevice, wd->Surface,
        requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat),
        requestSurfaceColorSpace
    );

    // 选择显示模式
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        g_PhysicalDevice, wd->Surface,
        present_modes, IM_COUNTOF(present_modes)
    );

    // 创建交换链和帧缓冲
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        g_Instance, g_PhysicalDevice, g_Device, wd,
        g_QueueFamily, g_Allocator,
        width, height, g_MinImageCount, 0
    );
}

// ==================== 清理函数 ====================
void CleanupVulkan() {
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd) {
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
    //vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
}

// ==================== 渲染函数 ====================
void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data) {
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX,
        image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
        return;
    }
    check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

    err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);
    err = vkResetFences(g_Device, 1, &fd->Fence);
    check_vk_result(err);

    err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
    check_vk_result(err);

    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
    check_vk_result(err);

    // 开始渲染通道
    {
        VkRenderPassBeginInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = wd->RenderPass;
        render_pass_info.framebuffer = fd->Framebuffer;
        render_pass_info.renderArea.extent.width = wd->Width;
        render_pass_info.renderArea.extent.height = wd->Height;
        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // 绘制 ImGui
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // 结束渲染通道并提交
    vkCmdEndRenderPass(fd->CommandBuffer);

    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_acquired_semaphore;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &fd->CommandBuffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &submit_info, fd->Fence);
        check_vk_result(err);
    }
}

void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild) return;

    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;

    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    check_vk_result(err);

    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

