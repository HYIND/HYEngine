#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/D2DRenderContext.h"
#include "RenderEngine/RenderFrameData.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "RenderEngine/RendererUI.h"

#include "VulkanRenderEngine/VulkanRenderer.h"

#include <functional>
#include <string>
#include <vector>

class Texture2D;
class RenderState;
class World;
class RenderSystem;

namespace Render
{
	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		// 三个子服务，只在需要具体类型时才取（例如宿主取相机快照）。
		Renderer3D* GetRenderer3D() noexcept { return _renderer3D.get(); }
		const Renderer3D* GetRenderer3D() const noexcept { return _renderer3D.get(); }
		Renderer2D* GetRenderer2D() noexcept { return _renderer2D.get(); }
		const Renderer2D* GetRenderer2D() const noexcept { return _renderer2D.get(); }
		// 后端与生命周期：3D 的设备、目标、帧循环都由这里统一转发，宿主只面对这一层；
		// UI 复用 3D 每帧产出的那张纹理，没有自己的目标、尺寸和格式。
		void SetVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer);
		bool CreateRenderTarget(uint32_t width, uint32_t height);
		bool ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize = {});

		bool Start();
		void StopFrameLoop();
		bool IsFrameLoopRunning() const;

		bool HasRenderer() const;
		bool IsRenderTargetReady() const;
		uint32_t GetRenderTargetWidth() const;
		uint32_t GetRenderTargetHeight() const;

		void Shutdown();

		// 宿主只面对这一个入口：等 3D 那一帧，顺带把 UI 叠上去，回调里拿到的就是合成结果。
		using WaitImageCallback = std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>;
		void WaitImage(const WaitImageCallback& callback);

		void BindRenderSystem(RenderSystem& renderSystem);
		void RegisterRenderSystem(World& world);

	private:
		std::unique_ptr<Renderer3D> _renderer3D;
		std::unique_ptr<Renderer2D> _renderer2D;
		std::unique_ptr<RendererUI> _uiRender;
	};
}
