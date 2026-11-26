#pragma once

#include "RenderEngine/RenderFrameData.h"

#include <memory>

class VKRenderer2D;

namespace Render
{
	// 引擎的 2D 层：Vulkan 渲染，叠在 3D 画面之上、UI 之下。
	// 2D 是帧流水线（VKFramePipeline）里接在 3D 后面的一个阶段：场景线程由 RenderSystem 把精灵 / 调试线 / 相机
	// 和 3D 一起写进同一份帧数据（RenderSceneFrameData），Render::Renderer 取帧时把 2D 那份带给这个阶段，
	// 所以 2D 和 3D 画的是同一个逻辑帧。编辑器和游戏走同一条路径，宿主不需要为 2D 做任何事。
	class Renderer2D
	{
	public:
		Renderer2D();
		~Renderer2D();

		Renderer2D(const Renderer2D&) = delete;
		Renderer2D& operator=(const Renderer2D&) = delete;

		// 流水线里的 2D 阶段（Vulkan 后端）。构造时不碰设备，管线在第一次真正要画时才建
		const std::shared_ptr<VKRenderer2D>& GetBackend() const { return _backend; }

		// 释放 GPU 资源（管线、实例缓冲）；调用前流水线必须已经停下。之后再画会重新建
		void Shutdown();

	private:
		std::shared_ptr<VKRenderer2D> _backend;
	};
}
