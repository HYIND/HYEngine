#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VKCommandBuffer.h"

namespace VKWrapper {
	class IVKResource
	{
	public:
		virtual ~IVKResource() = default;
		virtual void Destroy() = 0;
	};
} // namespace VKWrapper