#include "vkstdafx.h"
#include "RenderEngine/Renderer2D.h"

#include "VulkanRenderEngine/VKRenderer2D.h"

namespace Render
{
	Renderer2D::Renderer2D()
		: _backend(std::make_shared<VKRenderer2D>())
	{}

	Renderer2D::~Renderer2D()
	{
		Shutdown();
	}

	void Renderer2D::Shutdown()
	{
		// 后端对象本身留着：它已经作为阶段挂在流水线上，重新建流水线前不能换
		if (_backend)
			_backend->Release();
	}
}
