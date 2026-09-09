#include "vkstdafx.h"

#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>

#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE