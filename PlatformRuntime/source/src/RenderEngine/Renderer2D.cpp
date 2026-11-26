#include "vkstdafx.h"
#include "RenderEngine/Renderer2D.h"

#include "VulkanRenderEngine/VKRenderer2D.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

namespace Render
{
	Renderer2D::Renderer2D()
		: _frames(std::make_shared<FrameBuffer>())
	{
		for (int i = 0; i < 3; ++i)
			_frames->setInitialValue(i, std::make_shared<Render2DFrameData>());
	}

	Renderer2D::~Renderer2D()
	{
		Shutdown();
	}

	void Renderer2D::Render(const std::shared_ptr<Texture2D>& target)
	{
		if (!target || target->IsEmpty() || !_frames)
			return;

		auto& frame = _frames->acquireReadBuffer();
		if (!frame || (frame->sprites.empty() && frame->lines.empty()))
			return;

		// 管线依赖 Vulkan 设备，第一次真正要画时再建
		if (!_backend)
			_backend = std::make_unique<VKRenderer2D>();

		_backend->Render(target, *frame);
	}

	void Renderer2D::Shutdown()
	{
		if (_backend)
		{
			_backend->Release();
			_backend.reset();
		}
	}
}
