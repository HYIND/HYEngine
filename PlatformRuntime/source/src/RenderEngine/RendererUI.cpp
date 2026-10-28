#include "vkstdafx.h"
#include "RenderEngine/RendererUI.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

#include <utility>


#include <deque>

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
	{
		io.Fonts->AddFontDefault();
		m_font = io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
	}
	else
	{
		io.FontDefault = font;
		m_font = font;
	}

	// 粗体单独加载一个字重文件
	const char* boldFontPath = "C:/Windows/Fonts/msyhbd.ttc";
	m_boldFont = io.Fonts->AddFontFromFileTTF(boldFontPath, 18.0f, &fontConfig, glyphRanges);
	if (m_boldFont == nullptr)
		m_boldFont = m_font;

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

void RendererUI::FeedInput(float targetWidth, float targetHeight)
{
	if (!m_input)
		return;

	const EngineInputFrame frame = m_input->ReadFrame();
	ImGuiIO& io = ImGui::GetIO();

	// 窗口坐标 -> 渲染目标像素坐标；没有视口矩形时把鼠标挪到画面外（不命中任何节点）。
	float mouseX = -FLT_MAX;
	float mouseY = -FLT_MAX;
	if (frame.viewportWidth > 0.0f && frame.viewportHeight > 0.0f && targetWidth > 0.0f && targetHeight > 0.0f)
	{
		mouseX = (frame.mouseX - frame.viewportX) * (targetWidth / frame.viewportWidth);
		mouseY = (frame.mouseY - frame.viewportY) * (targetHeight / frame.viewportHeight);
	}

	io.AddMousePosEvent(mouseX, mouseY);
	io.AddMouseButtonEvent(0, frame.active && frame.IsKeyDown(EngineKey::MouseLeft));
	io.AddMouseButtonEvent(1, frame.active && frame.IsKeyDown(EngineKey::MouseRight));
	io.AddMouseButtonEvent(2, frame.active && frame.IsKeyDown(EngineKey::MouseMiddle));

	if (!frame.textInput.empty())
		io.AddInputCharactersUTF8(frame.textInput.c_str());

	// 滚轮：UI 列表/文本滚动靠它。
	if (frame.mouseWheel != 0.0f)
		io.AddMouseWheelEvent(0.0f, frame.mouseWheel);

	// 键盘：每帧同步一次电平，边沿和重复都由 ImGui 自己处理。
	const auto down = [&frame](EngineKey key) { return frame.active && frame.IsKeyDown(key); };
	const auto keyOf = [](EngineKey key) { return static_cast<int>(key); };

	// 字母与数字在两边都是连续段，直接按偏移映射。
	for (int i = 0; i < 26; ++i)
		io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_A + i), down(static_cast<EngineKey>(keyOf(EngineKey::A) + i)));
	for (int i = 0; i < 10; ++i)
		io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_0 + i), down(static_cast<EngineKey>(keyOf(EngineKey::Num0) + i)));
	for (int i = 0; i < 12; ++i)
		io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_F1 + i), down(static_cast<EngineKey>(keyOf(EngineKey::F1) + i)));

	const std::pair<EngineKey, ImGuiKey> plainKeys[] = {
		{ EngineKey::Escape, ImGuiKey_Escape }, { EngineKey::Tab, ImGuiKey_Tab },
		{ EngineKey::CapsLock, ImGuiKey_CapsLock }, { EngineKey::Space, ImGuiKey_Space },
		{ EngineKey::Enter, ImGuiKey_Enter }, { EngineKey::Backspace, ImGuiKey_Backspace },
		{ EngineKey::Delete, ImGuiKey_Delete }, { EngineKey::Insert, ImGuiKey_Insert },
		{ EngineKey::Home, ImGuiKey_Home }, { EngineKey::End, ImGuiKey_End },
		{ EngineKey::PageUp, ImGuiKey_PageUp }, { EngineKey::PageDown, ImGuiKey_PageDown },
		{ EngineKey::ArrowUp, ImGuiKey_UpArrow }, { EngineKey::ArrowDown, ImGuiKey_DownArrow },
		{ EngineKey::ArrowLeft, ImGuiKey_LeftArrow }, { EngineKey::ArrowRight, ImGuiKey_RightArrow },
		{ EngineKey::LeftShift, ImGuiKey_LeftShift }, { EngineKey::RightShift, ImGuiKey_RightShift },
		{ EngineKey::LeftCtrl, ImGuiKey_LeftCtrl }, { EngineKey::RightCtrl, ImGuiKey_RightCtrl },
		{ EngineKey::LeftAlt, ImGuiKey_LeftAlt }, { EngineKey::RightAlt, ImGuiKey_RightAlt },
		{ EngineKey::Minus, ImGuiKey_Minus }, { EngineKey::Equal, ImGuiKey_Equal },
		{ EngineKey::LeftBracket, ImGuiKey_LeftBracket }, { EngineKey::RightBracket, ImGuiKey_RightBracket },
		{ EngineKey::Backslash, ImGuiKey_Backslash }, { EngineKey::Semicolon, ImGuiKey_Semicolon },
		{ EngineKey::Quote, ImGuiKey_Apostrophe }, { EngineKey::Comma, ImGuiKey_Comma },
		{ EngineKey::Period, ImGuiKey_Period }, { EngineKey::Slash, ImGuiKey_Slash },
		{ EngineKey::Grave, ImGuiKey_GraveAccent },
	};
	for (const auto& [engineKey, imguiKey] : plainKeys)
		io.AddKeyEvent(imguiKey, down(engineKey));

	// 组合键：ImGui 要求修饰键单独再报一次。
	io.AddKeyEvent(ImGuiMod_Ctrl, down(EngineKey::LeftCtrl) || down(EngineKey::RightCtrl));
	io.AddKeyEvent(ImGuiMod_Shift, down(EngineKey::LeftShift) || down(EngineKey::RightShift));
	io.AddKeyEvent(ImGuiMod_Alt, down(EngineKey::LeftAlt) || down(EngineKey::RightAlt));
}

void RendererUI::DrawNode(ImDrawList* drawList, const UI::UiNode& node, const UI::ParseResult& parsed,
	UI::UiInstanceState* instance, float scaleX, float scaleY, float offsetX, float offsetY)
{
	const UI::ResolvedNodeStyle* resolved = nullptr;
	auto it = parsed.resolved.find(&node);
	if (it != parsed.resolved.end())
		resolved = &it->second;

	// 这一帧该节点的最终外观：基础态 + 交互状态覆盖。
	UI::StyleFields fields;
	if (resolved)
		fields = resolved->effective;

	const float x = offsetX + node.rect.x * scaleX;
	const float y = offsetY + node.rect.y * scaleY;
	const float w = node.rect.w * scaleX;
	const float h = node.rect.h * scaleY;

	// 实例状态：visible / enabled 是节点级基础属性，最先判定。
	// visible = false 时整棵子树都不画（下面的 children 递归根本走不到），也不参与命中。
	UI::UiNodeState& nodeState = instance ? instance->Get(node.id) : m_fallbackState;
	if (!nodeState.visible)
		return;

	const bool declaresClick = !node.onClick.command.empty() || node.onClick.legacyId != 0;
	const bool declaresChange = !node.onChange.command.empty() || node.onChange.legacyId != 0;

	// 这些类型自己就是 ImGui 控件，命中判定归控件自己，外面不要再套一层不可见按钮。
	const bool ownsWidget = node.type == "textinput" || node.type == "combo" || node.type == "scrolllist";

	// 只要声明了 onClick / onChange，任何类型都该能被点中：
	// panel / image / text / textblock / progress 以前根本没做命中测试，事件永远不会发。
	// enabled = false 的节点不参与命中，视觉上再压一半透明度（下面 opacity 处）。
	const bool hitTestable = nodeState.enabled && (declaresClick || declaresChange || node.type == "button" ||
		node.type == "toggle" || node.type == "slider") && !ownsWidget && w > 0.0f && h > 0.0f;

	bool hovered = false;
	bool active = false;
	bool clicked = false;
	if (hitTestable)
	{
		ImGui::PushID(node.id);
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::InvisibleButton("##ui_node", ImVec2(w, h));
		hovered = ImGui::IsItemHovered();
		active = ImGui::IsItemActive();

		if (resolved)
		{
			if (hovered)
			{
				auto state = resolved->states.find("hover");
				if (state != resolved->states.end())
					UI::MergeFields(fields, state->second);
			}
			if (active)
			{
				auto state = resolved->states.find("pressed");
				if (state != resolved->states.end())
					UI::MergeFields(fields, state->second);
			}
		}

		// 按下并在同一个节点上松开，才算一次点击。
		clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		ImGui::PopID();
	}

	// 所有类型的 onClick 统一在这一处发；控件自己的 onChange 在各自分支里发，
	// 因此 toggle / slider 可以同时声明两者。
	if (clicked && declaresClick)
		m_events.push_back(UI::UiEvent{ node.onClick.command, node.onClick.legacyId, node.name, 0.0f });

	const UI::StyleFields* style = &fields;

	const float opacity = (style->opacity ? *style->opacity : 1.0f) * (nodeState.enabled ? 1.0f : 0.5f);
	const float rounding = style->cornerRadius ? (*style->cornerRadius) * std::min(scaleX, scaleY) : 0.0f;

	ImU32 fill = 0;
	if (style->tint)
	{
		const UI::Vec4& tint = *style->tint;
		fill = ImGui::ColorConvertFloat4ToU32(ImVec4(tint.r, tint.g, tint.b, tint.a * opacity));
	}

	ImU32 textColor = IM_COL32(255, 255, 255, static_cast<int>(255.0f * opacity));
	if (style->color)
	{
		const UI::Vec4& color = *style->color;
		textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a * opacity));
	}

	if (fill != 0 && node.type != "text")
		drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, rounding);

	// ---- 滚动列表：行数据来自实例，滚动交给 ImGui 子窗口 ----
	if (node.type == "scrolllist")
	{
		const float rowHeight = std::max(1.0f, std::round((node.rowHeight > 0.0f ? node.rowHeight : 44.0f) * scaleY));
		std::vector<UI::UiListItem> fallbackItems;
		const std::vector<UI::UiListItem>* items = nullptr;
		if (instance)
		{
			auto it = instance->lists.find(node.id);
			if (it != instance->lists.end())
				items = &it->second;
		}
		if (!items)
		{
			// 没有运行时数据时，用文档里 children 的 text 当静态行（写死的演示/占位列表）。
			for (const auto& child : node.children)
			{
				if (child.text.empty())
					continue;

				UI::UiListItem row;
				row.text = child.text;
				fallbackItems.push_back(std::move(row));
			}
			items = &fallbackItems;
		}

		if (!nodeState.enabled)
			ImGui::BeginDisabled();

		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::PushID(node.id);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		if (ImGui::BeginChild("##ui_scrolllist", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_None))
		{
			const ImU32 rowText = textColor;
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(255, 255, 255, 40));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 64));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 96));
			ImGui::PushStyleColor(ImGuiCol_Text, rowText);

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(items->size()), rowHeight);
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					const UI::UiListItem& item = (*items)[i];
					const bool disabled = item.disabled;
					if (disabled)
						ImGui::BeginDisabled();

					ImGui::PushID(i);

					// selected 决定这个列表能不能点选：
					//   true  = 可交互列表，点中的行高亮并回传（下标在 value 里，和 combo 同一套规则）
					//   false = 展示列表，行只是"能看"，不高亮也不发事件
					// 行能不能点 = 资产说可以（selectable，默认 true）且运行期没被关掉。
					const bool selectable = node.selectable && nodeState.selected;
					const bool rowSelected = selectable && i == static_cast<int>(nodeState.value);

					if (ImGui::Selectable(item.text.c_str(), rowSelected,
							selectable ? ImGuiSelectableFlags_AllowDoubleClick : ImGuiSelectableFlags_Disabled,
							ImVec2(0.0f, rowHeight)))
					{
						const UI::UiEvent& event = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ? node.onClick : node.onChange;
						if (!event.command.empty() || event.legacyId != 0)
							m_events.push_back(UI::UiEvent{ event.command, event.legacyId, node.name, static_cast<float>(i), item.id });
					}
					ImGui::PopID();

					if (disabled)
						ImGui::EndDisabled();
				}
			}

			ImGui::PopStyleColor(4);
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::PopID();

		if (!nodeState.enabled)
			ImGui::EndDisabled();
		return;
	}

	// ---- 多行文本：行数据来自实例，默认粘底 ----
	if (node.type == "textblock")
	{
		std::deque<std::string> fallbackLines;
		const std::deque<std::string>* lines = nullptr;
		if (instance)
		{
			auto it = instance->texts.find(node.id);
			if (it != instance->texts.end())
				lines = &it->second;
		}
		if (!lines)
		{
			// 没有运行时数据时，用文档里 children 的 text 当静态行。
			for (const auto& child : node.children)
			{
				if (!child.text.empty())
					fallbackLines.push_back(child.text);
			}
			if (fallbackLines.empty())
			{
				// 连静态行都没有：造几行假消息，方便先看排版效果。
				for (int i = 0; i < 12; ++i)
					fallbackLines.push_back("[系统] 测试消息 " + std::string(i < 9 ? "0" : "") + std::to_string(i + 1)
						+ "：这是一行占位文本，用来观察换行、行距与滚动。");
			}
			lines = &fallbackLines;
		}

		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::PushID(node.id);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::BeginChild("##ui_textblock", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_None))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
			for (const auto& line : *lines)
				ImGui::TextWrapped("%s", line.c_str());

			// 贴底：只有本来就在底部时才跟随新行，用户上翻时不打扰。
			const float scrollY = ImGui::GetScrollY();
			const float scrollMax = ImGui::GetScrollMaxY();
			if (scrollMax <= 0.0f || scrollY >= scrollMax - 4.0f)
				ImGui::SetScrollHereY(1.0f);

			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopID();
		return;
	}

	// ---- 下拉框：关闭态自己画（吃 tint/圆角/文字样式），展开列表交给 ImGui 的 popup ----
	if (node.type == "combo")
	{
		UI::UiNodeState& comboState = instance ? instance->Get(node.id) : m_fallbackState;

		if (!nodeState.enabled)
			ImGui::BeginDisabled();

		// 选项表可以被外部整体替换（按语言/等级换一组选项），没替换过就用资产里声明的。
		const std::vector<UI::UiOption>& comboOptions = comboState.hasOptions ? comboState.options : node.options;

		// 实例里存的是"选项值"，这里反查成下标去画（下标不是状态的一部分，
		// 否则外部 SetValue 和 onChange 回传的 option.value 就是两套语义了）。
		int selected = 0;
		if (!comboOptions.empty())
		{
			const int optionValue = static_cast<int>(comboState.value);
			for (int i = 0; i < static_cast<int>(comboOptions.size()); ++i)
			{
				if (comboOptions[static_cast<size_t>(i)].value == optionValue)
				{
					selected = i;
					break;
				}
			}
		}

		const char* preview = comboOptions.empty() ? "" : comboOptions[static_cast<size_t>(selected)].text.c_str();

		ImFont* comboFont = (style->bold && *style->bold && m_boldFont) ? m_boldFont : (m_font ? m_font : ImGui::GetFont());
		const float comboFontSize = std::round((style->fontSize ? *style->fontSize : 20.0f) * scaleY);
		const float comboPadY = std::max(0.0f, std::round((h - comboFontSize) * 0.5f));
		const float comboPadX = std::round(std::min(scaleX, scaleY) * 10.0f) + 2.0f;

		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::PushID(node.id);
		ImGui::PushFont(comboFont, comboFontSize);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(comboPadX, comboPadY));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		// 关闭态的背景我们自己画，这里把 ImGui 的框和箭头都隐掉。
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 16));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 24));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 28, 36, 245));
		ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(64, 120, 200, 180));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(76, 140, 220, 200));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(90, 160, 240, 220));
		ImGui::SetNextItemWidth(w);

		const bool comboOpen = ImGui::BeginCombo("##ui_combo", preview, ImGuiComboFlags_HeightLarge | ImGuiComboFlags_NoArrowButton);
		const bool comboClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		if (comboOpen)
		{
			for (int i = 0; i < static_cast<int>(comboOptions.size()); ++i)
			{
				const UI::UiOption& option = comboOptions[static_cast<size_t>(i)];
				const bool isSelected = (i == selected);

				if (ImGui::Selectable(option.text.c_str(), isSelected))
				{
					// 存选项值，不存下标：和事件里回传的 option.value 保持同一套语义。
					comboState.value = static_cast<float>(option.value);
					if (!node.onChange.command.empty())
						m_events.push_back(UI::UiEvent{ node.onChange.command, node.onChange.legacyId, node.name, static_cast<float>(option.value), option.text });
				}

				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		// 展开下拉本身也算一次 onClick；选项变化仍然走 onChange。
		if (comboClicked && !node.onClick.command.empty())
			m_events.push_back(UI::UiEvent{ node.onClick.command, node.onClick.legacyId, node.name, 0.0f });

		ImGui::PopStyleColor(8);
		ImGui::PopStyleVar(3);
		ImGui::PopFont();

		// 右侧小三角：提示可以展开（ImGui 自带的箭头跟着 FrameBg 一起被透明掉了）。
		const float unit = std::min(scaleX, scaleY);
		const float arrowX = x + w - std::round(unit * 18.0f);
		const float arrowY = y + h * 0.5f;
		const float arrowSize = std::round(unit * 6.0f);
		drawList->AddTriangleFilled(ImVec2(arrowX - arrowSize, arrowY - arrowSize * 0.6f),
			ImVec2(arrowX + arrowSize, arrowY - arrowSize * 0.6f),
			ImVec2(arrowX, arrowY + arrowSize * 0.8f), textColor);

		ImGui::PopID();

		if (!nodeState.enabled)
			ImGui::EndDisabled();

		for (const auto& child : node.children)
			DrawNode(drawList, child, parsed, instance, scaleX, scaleY, offsetX, offsetY);
		return;
	}

	// ---- 文本输入：编辑行为交给 ImGui::InputText，背景和 placeholder 自己画 ----
	if (node.type == "textinput")
	{
		UI::UiNodeState& inputState = instance ? instance->Get(node.id) : m_fallbackState;

		if (!nodeState.enabled)
			ImGui::BeginDisabled();

		ImFont* inputFont = (style->bold && *style->bold && m_boldFont) ? m_boldFont : (m_font ? m_font : ImGui::GetFont());
		const float inputFontSize = std::round((style->fontSize ? *style->fontSize : 20.0f) * scaleY);
		const float padY = std::max(0.0f, std::round((h - inputFontSize) * 0.5f));
		const float padX = std::round(std::min(scaleX, scaleY) * 10.0f) + 2.0f;

		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::PushID(node.id);
		ImGui::PushFont(inputFont, inputFontSize);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		// 背景我们自己画过了：输入框本身只画文字和光标。
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::SetNextItemWidth(w);

		const bool bufferEmpty = inputState.text[0] == '\0';
		ImGui::InputText("##ui_textinput", inputState.text.data(), inputState.text.size());
		const bool inputActive = ImGui::IsItemActive();

		// 点进输入框也算一次 onClick（聚焦时触发），模块可以据此做"开始输入"。
		if (!node.onClick.command.empty() && ImGui::IsItemClicked())
			m_events.push_back(UI::UiEvent{ node.onClick.command, node.onClick.legacyId, node.name, 0.0f });

		if (ImGui::IsItemDeactivatedAfterEdit() && !node.onChange.command.empty())
			m_events.push_back(UI::UiEvent{ node.onChange.command, node.onChange.legacyId, node.name, 0.0f, std::string(), inputState.text.data() });

		// maxLength：超出的部分直接截掉（InputText 只受缓冲容量限制）。
		if (node.maxLength > 0 && static_cast<int>(std::strlen(inputState.text.data())) > node.maxLength)
			inputState.text[node.maxLength] = '\0';

		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar(3);
		ImGui::PopFont();
		ImGui::PopID();

		if (!nodeState.enabled)
			ImGui::EndDisabled();

		// placeholder：为空且没在编辑时自己画（ImGui 的 hint 颜色不能单独配）。
		if (bufferEmpty && !inputActive && !node.placeholder.empty())
		{
			const ImU32 placeholderColor = (textColor & 0x00FFFFFFu) | (static_cast<ImU32>(255.0f * opacity * 0.45f) << 24);
			const ImVec2 hintSize = inputFont->CalcTextSizeA(inputFontSize, FLT_MAX, 0.0f, node.placeholder.c_str());
			drawList->AddText(inputFont, inputFontSize,
				ImVec2(std::round(x + padX), std::round(y + (h - hintSize.y) * 0.5f)),
				placeholderColor, node.placeholder.c_str());
		}

		for (const auto& child : node.children)
			DrawNode(drawList, child, parsed, instance, scaleX, scaleY, offsetX, offsetY);
		return;
	}

	UI::UiNodeState& state = nodeState;

	const float rangeMin = node.minValue;
	const float rangeMax = (node.maxValue > node.minValue) ? node.maxValue : (node.minValue + 1.0f);
	const bool ranged = node.type == "slider" || node.type == "progress";

	// 滑块：按住时按鼠标位置换算成值，按 step 吸附。
	bool sliderDragged = false;
	if (node.type == "slider" && active && w > 0.0f)
	{
		const float t = std::clamp((ImGui::GetIO().MousePos.x - x) / w, 0.0f, 1.0f);
		float next = rangeMin + t * (rangeMax - rangeMin);
		if (node.step > 0.0f)
			next = rangeMin + std::round((next - rangeMin) / node.step) * node.step;
		next = std::clamp(next, rangeMin, rangeMax);

		// 拖动过程中值真的变了才算一次 onChange，避免每帧刷同样的值。
		if (next != state.value)
		{
			state.value = next;
			sliderDragged = true;
		}
	}

	float ratio = ranged ? (state.value - rangeMin) / (rangeMax - rangeMin) : 0.0f;
	ratio = std::clamp(ratio, 0.0f, 1.0f);

	// 前景色：优先用样式里的 color（通常和底色对比明显），没有再退回白色。
	const ImU32 accent = textColor;

	if (node.type == "progress" || node.type == "slider")
	{
		if (ratio > 0.0f)
			drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w * ratio, y + h), accent, rounding);

		if (node.type == "slider")
		{
			const float knobRadius = std::max(h * 0.55f, 6.0f);
			const float knobX = std::clamp(x + w * ratio, x + knobRadius, x + w - knobRadius);
			drawList->AddCircleFilled(ImVec2(knobX, y + h * 0.5f), knobRadius, IM_COL32(245, 248, 255, 235));

			if ((clicked || sliderDragged) && !node.onChange.command.empty())
				m_events.push_back(UI::UiEvent{ node.onChange.command, node.onChange.legacyId, node.name, state.value });
		}
	}
	else if (node.type == "toggle")
	{
		if (clicked)
		{
			state.toggled = !state.toggled;
			if (!node.onChange.command.empty())
				m_events.push_back(UI::UiEvent{ node.onChange.command, node.onChange.legacyId, node.name, state.toggled ? 1.0f : 0.0f });
		}

		// 钮：h 的 10% 留边，直径 = h - 2*pad，开时靠右、关时靠左。
		const float pad = std::max(h * 0.10f, 1.0f);
		const float radius = std::max((h - pad * 2.0f) * 0.5f, 1.0f);
		const float centerX = state.toggled ? (x + w - pad - radius) : (x + pad + radius);
		drawList->AddCircleFilled(ImVec2(centerX, y + h * 0.5f), radius, IM_COL32(248, 250, 255, 240));

		// 打开时描一圈亮边，状态一眼可见。
		if (state.toggled)
			drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), accent, rounding, 0, 2.0f);
	}

	// 文本 / 按钮文字：资产里的文字 → 外部设置的 label → 绑定值（绑定最后覆盖）
	std::string label = node.text;
	if (nodeState.hasLabel)
		label = nodeState.label;
	// 绑定：从别的节点的运行时值格式化出文本（例如滑动条右边的百分比）。
	if (node.bind && node.bind->targetId != 0)
	{
		const UI::UiNodeState& boundState = instance ? instance->Get(node.bind->targetId) : m_fallbackState;
		char boundBuffer[64];
		std::snprintf(boundBuffer, sizeof(boundBuffer),
			node.bind->format.empty() ? "%.0f" : node.bind->format.c_str(), static_cast<double>(boundState.value));
		label = boundBuffer;
	}
	if (node.type == "button" && label.empty())
		label = node.name;

	if (node.type == "progress" && node.showPercent && label.empty())
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(ratio * 100.0f)));
		label = buffer;
	}
	if (node.type == "textinput" && label.empty())
		label = node.placeholder;

	if (!label.empty())
	{
		const bool wantBold = style->bold && *style->bold;
		ImFont* font = (wantBold && m_boldFont) ? m_boldFont : (m_font ? m_font : ImGui::GetFont());

		// 字号取整：非整数字号会让字形边缘重新采样，看起来发虚。
		const float fontSize = std::round((style->fontSize ? *style->fontSize : 20.0f) * scaleY);
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());

		float textX = x;
		float textY = y + (h - textSize.y) * 0.5f;
		const std::string align = style->align ? *style->align : "center";
		if (align == "center")
			textX = x + (w - textSize.x) * 0.5f;
		else if (align == "right")
			textX = x + w - textSize.x;

		// 对齐到整数像素：ImGui 的文字是按像素栅格的，落在半个像素上就会重影/发虚。
		textX = std::round(textX);
		textY = std::round(textY);

		drawList->AddText(font, fontSize, ImVec2(textX, textY), textColor, label.c_str());
	}

	for (const auto& child : node.children)
		DrawNode(drawList, child, parsed, instance, scaleX, scaleY, offsetX, offsetY);
}

std::vector<UI::UiEvent> RendererUI::PollEvents()
{
	std::vector<UI::UiEvent> events;
	events.swap(m_events);
	return events;
}

void RendererUI::DrawUiContent(const std::shared_ptr<RenderUIFrameData>& frame)
{
	if (!frame || frame->views.empty())
		return;

	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
		return;

	// 用一个全屏隐形窗口承载交互控件；视觉仍然画在窗口的 draw list 上。
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(displaySize);
	const ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (!ImGui::Begin("##EngineUIRoot", nullptr, windowFlags))
	{
		ImGui::End();
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	int viewIndex = 0;

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

		// 同一份文档可能被多个 UIView 复用，节点 id 只在文档内唯一，必须按视图再隔离一层。
		ImGui::PushID(viewIndex++);
		{
			std::unique_lock<CriticalSectionLock> instanceLock;
			if (view.instance)
				instanceLock = std::unique_lock<CriticalSectionLock>(view.instance->mutex);

			DrawNode(drawList, document.root, *view.document, view.instance.get(), scaleX, scaleY, offsetX, offsetY);
		}
		ImGui::PopID();
	}

	ImGui::End();
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

	FeedInput(static_cast<float>(width), static_cast<float>(height));

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
