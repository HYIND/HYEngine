#pragma once

#include "vkstdafx.h"

#include <chrono>
#include <memory>

#include "Helper/TripleBuffer.h"
#include "RenderEngine/RenderFrameData.h"

struct ImGuiContext;
struct ImDrawList;
class Texture2D;

namespace Render
{
	// UI 叠加层：只做一件事——拿一张现成的纹理，把 ImGui 这一帧画在它上面。
	//
	// 没有自己的目标纹理、没有尺寸、没有格式：
	//   target 是谁，就画在谁身上（loadOp=Load，靠 ImGui 的预乘 alpha 叠上去）；
	//   DisplaySize 每帧从 target 取；格式也只看 target，变了就把后端管线重建一次。
	//
	// ImGui 上下文与 Vulkan 后端都是懒加载：第一次 Render 时才建，宿主不需要初始化它。
	// 用的是独立的 ImGuiContext，和编辑器自己那套互不影响。
	//
	// 现在还没有“画什么”的来源：等 RenderSystem 侧把 UIFrameData（UI 资产/布局组件
	// 收集出来的那一帧）推送过来，DrawUiContent 按它画。在那之前这里只走空帧。
	class RendererUI
	{
	public:
		RendererUI();
		~RendererUI();

		RendererUI(const RendererUI&) = delete;
		RendererUI& operator=(const RendererUI&) = delete;

		void Render(const std::shared_ptr<Texture2D>& target);
		void Shutdown();

		// 帧数据通道：RenderSystem 往这里写，RendererUI 在 Render 时取最新一份。
		using FrameBuffer = TripleBuffer<std::shared_ptr<RenderUIFrameData>>;
		std::shared_ptr<FrameBuffer> GetFrameBuffer() const { return m_frameBuffer; }

	private:
		// 按 RenderUIFrameData 画这一帧的内容。
		void DrawUiContent(const std::shared_ptr<RenderUIFrameData>& frame);
		void DrawNode(::ImDrawList* drawList, const UI::UiNode& node,
			const UI::ParseResult& parsed, float scaleX, float scaleY, float offsetX, float offsetY);

		bool EnsureContext();
		// ImGui 的 Vulkan 后端跟着颜色格式建管线：格式变了要重建一次。
		bool EnsureBackend(vk::Format format);
		void ShutdownBackend();

	private:
		ImGuiContext* m_context = nullptr;

		vk::Format m_format = vk::Format::eUndefined;
		bool m_backendReady = false;

		std::chrono::steady_clock::time_point m_lastFrameTime{};

		std::shared_ptr<FrameBuffer> m_frameBuffer;
	};
}
