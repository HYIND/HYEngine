#include "vkstdafx.h"
#include "RenderEngine/RendererUI.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include "imgui_impl_vulkan.h"

using namespace Render;

RendererUI::RendererUI()
{
	// 和 Renderer3D 一样：帧数据缓冲由渲染端持有，RenderSystem 在 bind 时注入引用。
	m_frameBuffer = std::make_shared<FrameBuffer>();
	for (int i = 0; i < 3; ++i)
		m_frameBuffer->setInitialValue(i, std::make_shared<RenderUIFrameData>());
}

RendererUI::~RendererUI()
{
	Shutdown();
}

bool RendererUI::EnsureContext()
{
	if (m_context)
		return true;

	IMGUI_CHECKVERSION();
	m_context = ImGui::CreateContext();
	if (!m_context)
		return false;

	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(m_context);

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	const char* fontPath = "C:/Windows/Fonts/msyh.ttc";
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;
	fontConfig.PixelSnapH = true;

	static const ImWchar glyphRanges[] = {
		0x0020, 0x00FF,  // 基本拉丁 + 标点
		0x4E00, 0x9FA5,  // 常用汉字（CJK 统一表意文字）
		0
	};

	ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, &fontConfig, glyphRanges);
	if (font == nullptr)
		io.Fonts->AddFontDefault();
	else
		io.FontDefault = font;

	ImGui::SetCurrentContext(previous);

	m_backendReady = false;
	m_format = vk::Format::eUndefined;
	return true;
}

void RendererUI::ShutdownBackend()
{
	if (!m_backendReady)
		return;

	if (m_context)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui_ImplVulkan_Shutdown();
		ImGui::SetCurrentContext(previous);
	}

	m_backendReady = false;
}

bool RendererUI::EnsureBackend(vk::Format format)
{
	if (m_backendReady && m_format == format)
		return true;

	ShutdownBackend();

	if (!VKCONTEXT || !VKCONTEXT->GetDevice())
		return false;

	m_format = format;

	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(m_context);

	VkFormat colorFormat = static_cast<VkFormat>(format);

	VkPipelineRenderingCreateInfoKHR pipelineRenderingInfo{};
	pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	pipelineRenderingInfo.colorAttachmentCount = 1;
	pipelineRenderingInfo.pColorAttachmentFormats = &colorFormat;

	ImGui_ImplVulkan_InitInfo info{};
	info.ApiVersion = VK_API_VERSION_1_3;
	info.Instance = VKCONTEXT->GetInstanceHandle();
	info.PhysicalDevice = static_cast<VkPhysicalDevice>(VKCONTEXT->GetDevice()->GetPhysicalDevice());
	info.Device = static_cast<VkDevice>(VKCONTEXT->GetDeviceHandle());
	info.QueueFamily = VKCONTEXT->GetDevice()->GetGraphicsQueueFamily();
	info.Queue = static_cast<VkQueue>(VKCONTEXT->GetDevice()->GetGraphicsQueue());
	info.DescriptorPoolSize = 64;
	info.MinImageCount = 2;
	info.ImageCount = 2;
	info.UseDynamicRendering = true;		// 动态渲染路径
	info.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;

	const bool ok = ImGui_ImplVulkan_Init(&info);
	ImGui::SetCurrentContext(previous);

	if (!ok)
		return false;

	m_backendReady = true;
	return true;
}

void RendererUI::Shutdown()
{
	ShutdownBackend();

	if (VKCONTEXT && VKCONTEXT->GetDevice())
		VKCONTEXT->GetDeviceHandle().waitIdle();

	if (m_context)
	{
		ImGui::DestroyContext(m_context);
		m_context = nullptr;
	}

	m_format = vk::Format::eUndefined;
	m_lastFrameTime = {};
}

void RendererUI::DrawNode(ImDrawList* drawList, const UI::UiNode& node, const UI::ParseResult& parsed,
	float scaleX, float scaleY, float offsetX, float offsetY)
{
	const UI::ResolvedNodeStyle* resolved = nullptr;
	auto it = parsed.resolved.find(&node);
	if (it != parsed.resolved.end())
		resolved = &it->second;

	const UI::StyleFields* style = resolved ? &resolved->effective : nullptr;

	const float x = offsetX + node.rect.x * scaleX;
	const float y = offsetY + node.rect.y * scaleY;
	const float w = node.rect.w * scaleX;
	const float h = node.rect.h * scaleY;

	const float opacity = (style && style->opacity) ? *style->opacity : 1.0f;
	const float rounding = (style && style->cornerRadius) ? (*style->cornerRadius) * std::min(scaleX, scaleY) : 0.0f;

	ImU32 fill = 0;
	if (style && style->tint)
	{
		const UI::Vec4& tint = *style->tint;
		fill = ImGui::ColorConvertFloat4ToU32(ImVec4(tint.r, tint.g, tint.b, tint.a * opacity));
	}

	ImU32 textColor = IM_COL32(255, 255, 255, static_cast<int>(255.0f * opacity));
	if (style && style->color)
	{
		const UI::Vec4& color = *style->color;
		textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a * opacity));
	}

	if (fill != 0 && node.type != "text")
		drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, rounding);

	// 文本 / 按钮文字
	std::string label = node.text;
	if (node.type == "button" && label.empty())
		label = node.name;

	if (!label.empty())
	{
		ImFont* font = ImGui::GetFont();
		const float fontSize = (style && style->fontSize ? *style->fontSize : 20.0f) * scaleY;
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());

		float textX = x;
		float textY = y + (h - textSize.y) * 0.5f;
		const std::string align = (style && style->align) ? *style->align : "center";
		if (align == "center")
			textX = x + (w - textSize.x) * 0.5f;
		else if (align == "right")
			textX = x + w - textSize.x;

		drawList->AddText(font, fontSize, ImVec2(textX, textY), textColor, label.c_str());
	}

	for (const auto& child : node.children)
		DrawNode(drawList, child, parsed, scaleX, scaleY, offsetX, offsetY);
}

void RendererUI::DrawUiContent(const std::shared_ptr<RenderUIFrameData>& frame)
{
	if (!frame || frame->views.empty())
		return;

	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
		return;

	ImDrawList* drawList = ImGui::GetBackgroundDrawList();

	for (const auto& view : frame->views)
	{
		if (!view.document || !view.document->ok)
			continue;

		const UI::UiDocument& document = view.document->document;
		const float space = static_cast<float>(document.space > 0.0 ? document.space : 1000.0);

		float scaleX = displaySize.x / space;
		float scaleY = displaySize.y / space;
		float offsetX = 0.0f;
		float offsetY = 0.0f;

		// fit：等比缩放并居中；stretch（默认）：两轴各自拉伸。
		if (document.aspectMode == "fit")
		{
			const float scale = std::min(scaleX, scaleY);
			scaleX = scale;
			scaleY = scale;
			offsetX = (displaySize.x - space * scale) * 0.5f;
			offsetY = (displaySize.y - space * scale) * 0.5f;
		}

		DrawNode(drawList, document.root, *view.document, scaleX, scaleY, offsetX, offsetY);
	}
}

void RendererUI::Render(const std::shared_ptr<Texture2D>& target)
{
	if (!target || target->IsEmpty())
		return;

	if (!EnsureContext())
		return;

	if (!EnsureBackend(target->GetFormat()))
		return;

	const uint32_t width = target->GetWidth();
	const uint32_t height = target->GetHeight();

	float deltaMilliseconds = 16.0f;
	const auto now = std::chrono::steady_clock::now();
	if (m_lastFrameTime.time_since_epoch().count() != 0)
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime).count();
		deltaMilliseconds = static_cast<float>(elapsed) / 1000.0f;
		if (deltaMilliseconds <= 0.0f || deltaMilliseconds > 250.0f)
			deltaMilliseconds = 16.0f;
	}
	m_lastFrameTime = now;

	// 记录前一个上下文，以便后续恢复
	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(m_context);

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
	io.DeltaTime = deltaMilliseconds / 1000.0f;

	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();

	// 取一份就绪的 UI 帧数据（RenderSystem 每帧写、这里每帧读）。
	std::shared_ptr<RenderUIFrameData> frame;
	if (m_frameBuffer)
		frame = m_frameBuffer->acquireReadBuffer();

	DrawUiContent(frame);

	ImGui::Render();

	if (auto cmd = VKCONTEXT->GetCommandBuffer())
	{
		if (ImDrawData* drawData = ImGui::GetDrawData())
		{
			cmd->Begin();

			target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

			DynamicRenderInfo renderInfo{};
			renderInfo.SetRenderArea(width, height);
			renderInfo.AddColorAttachment(
				target->GetImageView(vk::ImageAspectFlagBits::eColor),
				vk::AttachmentLoadOp::eLoad,
				vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } });

			cmd->beginRendering(renderInfo);
			ImGui_ImplVulkan_RenderDrawData(drawData, cmd->GetHandle());
			cmd->endRendering();

			// 收尾成可采样状态
			target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);

			cmd->End();
			cmd->SubmitNowAndWait();
		}
	}

	ImGui::SetCurrentContext(previous);
}
