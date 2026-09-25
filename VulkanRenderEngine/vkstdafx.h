#pragma once

#define Enable_Vulkan_Validation

#define VK_USE_PLATFORM_WIN32_KHR

#include <windows.h>

#define VK_NO_PROTOTYPES
#include <volk/volk.h>


#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vma/vk_mem_alloc.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>

#include <vulkan/vk_enum_string_helper.h>


#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/norm.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <memory>
#include <iostream>
#include <format>
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <thread>
#include <functional>
#include <execution>
#include <string>
#include <atomic>
#include <array>
#include <algorithm>
#include <cstdint>
#include <variant>
#include <cmath>
#include <any>
#include <type_traits>
#include <stdexcept>
#include <condition_variable>
#include <future>
#include <deque>
#include <random>
#include <ranges>

#include "Helper/Tools.h"
#include "VulkanRenderEngine/Public.h"

using IndirectDrawCommand = vk::DrawIndexedIndirectCommand;