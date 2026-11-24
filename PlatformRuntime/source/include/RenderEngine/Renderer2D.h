#pragma once

#include "RenderEngine/RenderFrameData.h"
#include "Helper/TripleBuffer.h"

#include <memory>

class Texture2D;
class VKRenderer2D;

namespace Render
{
	// 引擎的 2D 层：Vulkan 渲染，叠在 3D 画面之上、UI 之下（见 Renderer::WaitImage）。
	// 场景线程由 RenderSystem 往帧缓冲里写精灵 / 调试线 / 相机，渲染线程在 Render 里取最新一份画到目标上。
	// 编辑器和游戏走同一条路径，宿主不需要为 2D 做任何事。
	class Renderer2D
	{
	public:
		using FrameBuffer = TripleBuffer<std::shared_ptr<Render2DFrameData>>;
		using Render2DFrameBufferPtr = std::shared_ptr<FrameBuffer>;

		Renderer2D();
		~Renderer2D();

		Renderer2D(const Renderer2D&) = delete;
		Renderer2D& operator=(const Renderer2D&) = delete;

		Render2DFrameBufferPtr GetFrameBuffer() const { return _frames; }

		// 把最新一帧 2D 内容叠到 target 上（同步完成后返回）
		void Render(const std::shared_ptr<Texture2D>& target);

		void Shutdown();

	private:
		Render2DFrameBufferPtr _frames;
		std::unique_ptr<VKRenderer2D> _backend;
	};
}
