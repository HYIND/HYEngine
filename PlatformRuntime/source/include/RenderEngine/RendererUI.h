#pragma once

#include "vkstdafx.h"

#include <chrono>
#include <memory>

#include "Helper/TripleBuffer.h"
#include "RenderEngine/RenderFrameData.h"
#include "Input/EngineInputContext.h"

#include <string>
#include <vector>

struct ImGuiContext;
struct ImDrawList;
struct ImFont;
class Texture2D;

namespace Render
{
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

		// 输入源：宿主把鼠标/文字喂进 EngineInputContext，这里每帧读一次翻成 ImGuiIO 事件。
		void SetInputContext(std::shared_ptr<EngineInputContext> context) { m_input = std::move(context); }

		// 这一帧 UI 上发生的交互事件（取走即清空）。事件结构体由 Common 的 UI::UiEvent 定义，
		// 渲染层只负责产出，派发是 UICommandBus 的事。
		std::vector<UI::UiEvent> PollEvents();

	private:
		// 按 RenderUIFrameData 画这一帧的内容。
		void DrawUiContent(const std::shared_ptr<RenderUIFrameData>& frame);
		void DrawNode(::ImDrawList* drawList, const UI::UiNode& node,
			const UI::ParseResult& parsed, UI::UiInstanceState* instance,
			float scaleX, float scaleY, float offsetX, float offsetY);

		// 把 EngineInputContext 里的鼠标/文字翻成 ImGuiIO 事件（坐标换算到渲染目标像素）。
		void FeedInput(float targetWidth, float targetHeight);

		bool EnsureContext();
		// ImGui 的 Vulkan 后端跟着颜色格式建管线：格式变了要重建一次。
		bool EnsureBackend(vk::Format format);
		void ShutdownBackend();

	private:
		ImGuiContext* m_context = nullptr;

		// 常规/粗体两套字形
		ImFont* m_font = nullptr;
		ImFont* m_boldFont = nullptr;

		vk::Format m_format = vk::Format::eUndefined;
		bool m_backendReady = false;

		std::chrono::steady_clock::time_point m_lastFrameTime{};

		std::shared_ptr<FrameBuffer> m_frameBuffer;
		std::shared_ptr<EngineInputContext> m_input;
		std::vector<UI::UiEvent> m_events;

		// 没有实例状态时的兜底（例如调试用的一帧数据没带 instance）。
		UI::UiNodeState m_fallbackState;
	};
}
