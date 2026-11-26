#include "ImguiLayout.h"

#include "Helper/FileIO.h"
#include "glm/gtc/type_ptr.hpp"
#include "CommonComponent.h"
#include "PropertiesHelper.h"
#include "DDGIEditor.h"
#include "RenderProfilerPanel.h"
#include "LogicProfilerPanel.h"
#include "RenderOptionPanels.h"
#include "IconManager.h"
#include "Sprite2DEditor.h"
#include "TextAssetEditor.h"
#include "AssetThumbnails.h"
#include "Render/Systems/RenderSystem.h"
#include "PlatformRuntimeComponents.h"
#include "SceneCodec/ComponentCodec.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <vector>
#include <mutex>
#include <optional>
#include "glm/gtc/matrix_transform.hpp"
#include "Coroutine.h"
#include "EditorTags.h"
#include "Input/EngineInputContext.h"

#include <Windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#include <string>

namespace fs = std::filesystem;

namespace
{
	// ---------------- 资产浏览器：筛选 + 搜索（只作用于当前文件夹显示的项，两者取交集） ----------------

	constexpr int kAssetTypeCount = static_cast<int>(AssetType::AnimatedImage) + 1;

	const char* BrowserTypeLabel(AssetType type)
	{
		switch (type)
		{
		case AssetType::Texture: return "贴图";
		case AssetType::Sprite: return "精灵";
		case AssetType::AnimatedImage: return "动图";
		case AssetType::StaticModel: return "模型";
		case AssetType::StaticMesh: return "网格体";
		case AssetType::SkinnedModel: return "蒙皮模型";
		case AssetType::SkinnedMesh: return "蒙皮网格";
		case AssetType::Skeleton: return "骨骼";
		case AssetType::AnimationClip: return "动画片段";
		case AssetType::Material: return "材质";
		case AssetType::UiDocument: return "UI 文档";
		case AssetType::UiStyle: return "UI 样式";
		case AssetType::Scene: return "场景";
		case AssetType::Script: return "脚本";
		case AssetType::Audio: return "音频";
		default: return "未知类型";
		}
	}

	struct BrowserFilterGroup
	{
		const char* name;
		std::vector<AssetType> types;
	};

	const std::vector<BrowserFilterGroup>& BrowserFilterGroups()
	{
		static const std::vector<BrowserFilterGroup> groups = {
			{ "图像", { AssetType::Texture, AssetType::Sprite, AssetType::AnimatedImage } },
			{ "3D", { AssetType::StaticModel, AssetType::StaticMesh, AssetType::SkinnedModel, AssetType::SkinnedMesh,
				AssetType::Skeleton, AssetType::AnimationClip, AssetType::Material } },
			{ "UI", { AssetType::UiDocument, AssetType::UiStyle } },
			{ "其他", { AssetType::Scene, AssetType::Script, AssetType::Audio, AssetType::Unknown } },
		};
		return groups;
	}

	struct BrowserFilterState
	{
		bool folders = true;
		bool nonAsset = false;
		bool types[kAssetTypeCount] = {};
		char search[256] = {};
		bool popupWasOpen = false;		// 上一帧气泡是否开着
		bool clickOnlyCloses = false;	// 按下按钮时气泡开着：这次点击只用来关它，不再重新打开

		BrowserFilterState() { Reset(); }

		// 默认：显示文件夹和所有资产类型，不显示非资产文件
		void Reset()
		{
			folders = true;
			nonAsset = false;
			for (bool& value : types)
				value = true;
		}
		void SelectAll()
		{
			folders = true;
			nonAsset = true;
			for (bool& value : types)
				value = true;
		}
		void Invert()
		{
			folders = !folders;
			nonAsset = !nonAsset;
			for (bool& value : types)
				value = !value;
		}
		// 和默认状态不同的开关个数（显示在按钮上）
		int ChangedCount() const
		{
			int count = (folders ? 0 : 1) + (nonAsset ? 1 : 0);
			for (bool value : types)
				count += value ? 0 : 1;
			return count;
		}
		bool& TypeFlag(AssetType type)
		{
			int index = static_cast<int>(type);
			if (index < 0 || index >= kAssetTypeCount)
				index = 0;
			return types[index];
		}
	};

	BrowserFilterState& BrowserFilter()
	{
		static BrowserFilterState state;
		return state;
	}

	// 搜索词：按空格拆开，英文转小写；每个词都要出现在名字里
	std::vector<std::string> SplitSearchTerms(const char* text)
	{
		std::vector<std::string> terms;
		std::string current;
		for (const char* p = text; p && *p; ++p)
		{
			const unsigned char c = static_cast<unsigned char>(*p);
			if (c == ' ' || c == '\t')
			{
				if (!current.empty())
					terms.push_back(std::move(current));
				current.clear();
			}
			else
				current.push_back(c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
		}
		if (!current.empty())
			terms.push_back(std::move(current));
		return terms;
	}

	bool MatchSearchTerms(const std::string& name, const std::vector<std::string>& terms)
	{
		if (terms.empty())
			return true;
		std::string lower(name);
		for (char& c : lower)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (u < 128)
				c = static_cast<char>(std::tolower(u));
		}
		for (const auto& term : terms)
		{
			if (lower.find(term) == std::string::npos)
				return false;
		}
		return true;
	}

	bool PassBrowserFilter(BrowserFilterState& filter, bool isFolder, bool isAsset, AssetType type)
	{
		if (isFolder)
			return filter.folders;
		if (!isAsset)
			return filter.nonAsset;
		return filter.TypeFlag(type);
	}

	// 带"混合"状态的组开关：全开时勾上、全关时不勾、部分开时显示混合；点一下在"全开 / 全关"之间切
	void GroupCheckbox(const char* label, BrowserFilterState& filter, const std::vector<AssetType>& types)
	{
		int on = 0;
		for (AssetType type : types)
			on += filter.TypeFlag(type) ? 1 : 0;
		bool all = on == static_cast<int>(types.size());
		const bool mixed = on > 0 && !all;
		if (mixed)
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		if (ImGui::Checkbox(label, &all))
		{
			for (AssetType type : types)
				filter.TypeFlag(type) = all;
		}
		if (mixed)
			ImGui::PopItemFlag();
	}

	void DrawBrowserFilterBar(BrowserFilterState& filter, size_t shown, size_t total)
	{
		const char* popupId = "##AssetBrowserFilterPopup";
		const int changed = filter.ChangedCount();

		// ---- 筛选按钮（右侧画一个小三角，字体里没有箭头字符）----
		if (changed > 0)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		const std::string label = changed > 0 ? std::format("筛选 ({})     ##AssetFilterButton", changed) : std::string("筛选     ##AssetFilterButton");
		const bool clicked = ImGui::Button(label.c_str());
		if (changed > 0)
			ImGui::PopStyleColor();
		const ImVec2 buttonMin = ImGui::GetItemRectMin();
		const ImVec2 buttonMax = ImGui::GetItemRectMax();
		if (ImGui::IsItemActivated())
			filter.clickOnlyCloses = filter.popupWasOpen;	// 点气泡外面会先把它关掉，所以要看按下这一刻之前它开没开
		if (clicked && !filter.clickOnlyCloses)
			ImGui::OpenPopup(popupId);
		if (ImGui::IsItemHovered() && !filter.popupWasOpen)
		{
			if (changed > 0)
				ImGui::SetTooltip("按类型筛选当前文件夹的内容（%d 项和默认不同）", changed);
			else
				ImGui::SetTooltip("按类型筛选当前文件夹的内容");
		}
		{
			const float h = buttonMax.y - buttonMin.y;
			const ImVec2 c(buttonMax.x - h * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f + h * 0.04f);
			const float r = h * 0.16f;
			ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.55f), ImVec2(c.x + r, c.y - r * 0.55f),
				ImVec2(c.x, c.y + r * 0.65f), ImGui::GetColorU32(ImGuiCol_Text));
		}

		// ---- 搜索栏 ----
		ImGui::SameLine();
		const std::string countText = std::format("显示 {} / {}", shown, total);
		const float countWidth = ImGui::CalcTextSize(countText.c_str()).x;
		const float clearWidth = filter.search[0] ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
		const float searchWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - countWidth - clearWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
		ImGui::SetNextItemWidth(searchWidth);
		ImGui::InputTextWithHint("##AssetBrowserSearch", "搜索当前文件夹（空格分隔多个词）", filter.search, sizeof(filter.search));
		if (filter.search[0])
		{
			ImGui::SameLine();
			if (ImGui::Button("×##AssetBrowserClearSearch", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
				filter.search[0] = '\0';
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", countText.c_str());

		// ---- 筛选气泡：带一个指向按钮的小三角；点外面 / 再点按钮 / Esc 关闭 ----
		// 面板里不用表格：嵌套表格放在自动适应大小的弹窗里，列宽会被算成 0，选项全被裁掉。
		// 改成按固定列宽手动排，面板尺寸可以在画之前算出来，摆放不依赖上一帧量到的大小
		const ImGuiStyle& style = ImGui::GetStyle();
		auto checkboxWidth = [&style](const char* text)
			{
				return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text, nullptr, true).x;
			};
		const char* fileRowLabel = "文件";
		float groupColumnWidth = ImGui::CalcTextSize(fileRowLabel).x;
		float cellWidth = std::max(checkboxWidth("文件夹"), checkboxWidth("非资产文件"));
		for (const auto& group : BrowserFilterGroups())
		{
			groupColumnWidth = std::max(groupColumnWidth, checkboxWidth(group.name));
			for (AssetType type : group.types)
				cellWidth = std::max(cellWidth, checkboxWidth(BrowserTypeLabel(type)));
		}
		groupColumnWidth += style.ItemSpacing.x * 2.0f;
		cellWidth += style.ItemSpacing.x;
		const float headerWidth = ImGui::CalcTextSize("全选").x + ImGui::CalcTextSize("反选").x + ImGui::CalcTextSize("重置").x
			+ style.FramePadding.x * 6.0f + style.ItemSpacing.x * 2.0f;

		// 可用区域：资产浏览器窗口（优先）和编辑器主窗口
		const float margin = 4.0f;
		const ImVec2 hostPos = ImGui::GetWindowPos();
		const ImVec2 hostSize = ImGui::GetWindowSize();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 screenMin(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin);
		const ImVec2 screenMax(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + viewport->WorkSize.y - margin);
		const float hostLeft = std::max(hostPos.x + margin, screenMin.x);
		const float hostRight = std::min(hostPos.x + hostSize.x - margin, screenMax.x);
		const float hostTop = std::max(hostPos.y + margin, screenMin.y);
		const float hostBottom = std::min(hostPos.y + hostSize.y - margin, screenMax.y);

		auto panelWidth = [&](int columns)
			{
				return std::max(headerWidth, groupColumnWidth + cellWidth * static_cast<float>(columns) - style.ItemSpacing.x)
					+ style.WindowPadding.x * 2.0f + style.ItemSpacing.x;
			};
		// 每行放几个类型：优先 4 个，资产浏览器窗口太窄就减少，最少 1 个
		int columns = 4;
		while (columns > 1 && panelWidth(columns) > hostRight - hostLeft)
			--columns;
		auto rowsOf = [columns](size_t count) { return std::max(1, static_cast<int>((count + columns - 1) / columns)); };

		int lines = 1 + rowsOf(2);	// 按钮行 + 文件行
		for (const auto& group : BrowserFilterGroups())
			lines += rowsOf(group.types.size());
		const int separators = 2;
		const float frameHeight = ImGui::GetFrameHeight();
		const float contentHeight = lines * frameHeight + (lines - 1) * style.ItemSpacing.y
			+ separators * (style.ItemSpacing.y + 1.0f) + style.WindowPadding.y * 2.0f + style.ItemSpacing.y;
		float width = panelWidth(columns);

		// 上下：先看资产浏览器窗口里放不放得下（下方优先），再看主窗口里；都不行放空间大的一边、限制高度（可以滚动）
		const float arrowHeight = 7.0f;
		const float belowY = buttonMax.y + arrowHeight;
		const float aboveBottom = buttonMin.y - arrowHeight;
		bool placeBelow = true;
		if (belowY + contentHeight <= hostBottom)
			placeBelow = true;
		else if (aboveBottom - contentHeight >= hostTop)
			placeBelow = false;
		else if (belowY + contentHeight <= screenMax.y)
			placeBelow = true;
		else if (aboveBottom - contentHeight >= screenMin.y)
			placeBelow = false;
		else
			placeBelow = (screenMax.y - belowY) >= (aboveBottom - screenMin.y);

		const float maxHeight = std::max(frameHeight * 3.0f, placeBelow ? screenMax.y - belowY : aboveBottom - screenMin.y);
		const float height = std::min(contentHeight, maxHeight);
		if (height < contentHeight)
			width += style.ScrollbarSize;	// 要滚动时给竖向滚动条留出宽度，免得最右一列被挡
		const float y = placeBelow ? belowY : aboveBottom - height;

		// 左右：对齐按钮左边，夹在资产浏览器窗口里（比窗口还宽时只夹在主窗口里）
		float x = buttonMin.x;
		if (hostRight - hostLeft >= width)
			x = std::clamp(x, hostLeft, hostRight - width);
		x = std::clamp(x, screenMin.x, std::max(screenMin.x, screenMax.x - width));

		// 尺寸显式给定（弹窗自动适应大小时，API 设过的轴以设的为准），位置和尺寸当帧就是对的
		ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
		bool open = false;
		if (ImGui::BeginPopup(popupId))
		{
			open = true;
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				ImGui::CloseCurrentPopup();

			if (ImGui::Button("全选"))
				filter.SelectAll();
			ImGui::SameLine();
			if (ImGui::Button("反选"))
				filter.Invert();
			ImGui::SameLine();
			if (ImGui::Button("重置"))
				filter.Reset();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("回到默认：显示文件夹和所有资产，不显示非资产文件");
			ImGui::Separator();

			// 每组一行（类型多了按列数换行）：左边组开关，右边这组的类型
			const float rowStartX = ImGui::GetCursorPosX();
			auto placeCell = [&](size_t index)
				{
					const int column = static_cast<int>(index % static_cast<size_t>(columns));
					if (column == 0 && index > 0)
						ImGui::SetCursorPosX(rowStartX + groupColumnWidth);	// 上一个选项已经换行
					else
						ImGui::SameLine(rowStartX + groupColumnWidth + cellWidth * static_cast<float>(column));
				};
			for (const auto& group : BrowserFilterGroups())
			{
				ImGui::PushID(group.name);
				GroupCheckbox(group.name, filter, group.types);
				for (size_t i = 0; i < group.types.size(); ++i)
				{
					placeCell(i);
					ImGui::Checkbox(BrowserTypeLabel(group.types[i]), &filter.TypeFlag(group.types[i]));
				}
				ImGui::PopID();
			}

			ImGui::Separator();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", fileRowLabel);
			placeCell(0);
			ImGui::Checkbox("文件夹", &filter.folders);
			placeCell(1);
			ImGui::Checkbox("非资产文件", &filter.nonAsset);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("没有登记成资产的文件：原始图片、fbx / obj、没登记的 .meta 等");

			// 指向按钮的小三角（画在气泡窗口外面，用前景层）：气泡在下方时画在上边缘、尖朝上；在上方时画在下边缘、尖朝下
			const ImVec2 popupPos = ImGui::GetWindowPos();
			const ImVec2 popupSize = ImGui::GetWindowSize();
			const float rounding = style.PopupRounding;
			const float tipMin = popupPos.x + rounding + arrowHeight + 2.0f;
			const float tipMax = std::max(tipMin, popupPos.x + popupSize.x - rounding - arrowHeight - 2.0f);
			const float tipX = std::clamp((buttonMin.x + buttonMax.x) * 0.5f, tipMin, tipMax);
			const float edgeY = placeBelow ? popupPos.y + 1.0f : popupPos.y + popupSize.y - 1.0f;
			const ImVec2 tip(tipX, placeBelow ? popupPos.y - arrowHeight + 1.0f : popupPos.y + popupSize.y + arrowHeight - 1.0f);
			const ImVec2 left(tipX - arrowHeight, edgeY);
			const ImVec2 right(tipX + arrowHeight, edgeY);
			ImDrawList* foreground = ImGui::GetForegroundDrawList();
			foreground->AddTriangleFilled(left, right, tip, ImGui::GetColorU32(ImGuiCol_PopupBg));
			foreground->AddLine(left, tip, ImGui::GetColorU32(ImGuiCol_Border));
			foreground->AddLine(tip, right, ImGui::GetColorU32(ImGuiCol_Border));

			ImGui::EndPopup();
		}
		filter.popupWasOpen = open;
	}

	// ---------------- 编辑器视口里的 2D 层：点选、手柄 ----------------

	// 最近一次从 2D 层点选到的实体：它用 2D 手柄直接改 Transform2D
	Entity s_pickedBy2D;

	// 2D 手柄每帧要用的相机：异步从场景线程取，界面用上一份结果，不阻塞界面线程
	struct Gizmo2DCameraCache
	{
		std::mutex mutex;
		VKRenderContext::Camera2DData camera;
		bool valid = false;
	};
	Gizmo2DCameraCache s_gizmo2DCamera;

	// 和 2D 渲染一致：按渲染目标的尺寸算可见区域（还没有目标时退回视口尺寸）
	VKRenderContext::View2D Viewport2DView(const VKRenderContext::Camera2DData& camera, EditorApp* app, const ImVec2& viewportSize)
	{
		uint32_t width = app->GetRenderTargetWidth();
		uint32_t height = app->GetRenderTargetHeight();
		if (width == 0 || height == 0)
		{
			width = static_cast<uint32_t>(std::max(1.0f, viewportSize.x));
			height = static_cast<uint32_t>(std::max(1.0f, viewportSize.y));
		}
		return VKRenderContext::ComputeView2D(camera, width, height);
	}

	// 鼠标位置 -> 2D 世界，点选最上面的 2D 实体
	Entity Pick2DAtMouse(EditorApp* app, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return Entity();

		const glm::vec2 uv((mouse.x - viewportPos.x) / viewportSize.x, (mouse.y - viewportPos.y) / viewportSize.y);
		Entity picked;
		world->SubmitCommand([&](World& w)
			{
				const auto view = Viewport2DView(RenderSystem::GetActiveCamera2D(w), app, viewportSize);
				picked = RenderSystem::Pick2D(w, view.ScreenToWorld(uv * view.targetSize));
			}).get();
		return picked;
	}

	// 选中的实体用不用 2D 手柄：从 2D 层点选到的，或者只有 Transform2D、没有 3D Transform 的
	bool Use2DGizmo(Entity entity)
	{
		if (!entity || !entity.isValid() || !entity.tryGetComponent<Transform2D>())
			return false;
		return entity == s_pickedBy2D || !entity.tryGetComponent<Transform>();
	}

	// 2D 手柄：平移 X/Y、绕 Z 旋转、缩放 X/Y，结果写回 Transform2D
	void Draw2DGizmo(EditorApp* app, Entity entity, const ImVec2& viewportSize, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, const float* snap)
	{
		World* world = app ? app->GetWorld() : nullptr;
		auto* transform = entity.tryGetComponent<Transform2D>();
		if (!world || !transform)
			return;

		world->SubmitCommand([](World& w)
			{
				const auto camera = RenderSystem::GetActiveCamera2D(w);
				std::lock_guard lock(s_gizmo2DCamera.mutex);
				s_gizmo2DCamera.camera = camera;
				s_gizmo2DCamera.valid = true;
			});

		VKRenderContext::Camera2DData camera;
		{
			std::lock_guard lock(s_gizmo2DCamera.mutex);
			if (!s_gizmo2DCamera.valid)
				return;
			camera = s_gizmo2DCamera.camera;
		}

		const auto view2D = Viewport2DView(camera, app, viewportSize);
		// 2D 世界 y 朝下、viewProj 是 Vulkan 裁剪空间；ImGuizmo 按 OpenGL 习惯（NDC y 朝上）映射到屏幕，所以把 y 翻回来
		const glm::mat4 projection = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)) * view2D.viewProj;
		const glm::mat4 view(1.0f);

		const glm::vec2 position = transform->position;
		const float rotation = transform->rotation;
		const glm::vec2 scale = transform->scale;
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f))
			* glm::rotate(glm::mat4(1.0f), glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(scale, 1.0f));

		ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y;
		if (operation == ImGuizmo::ROTATE)
			op = ImGuizmo::ROTATE_Z;
		else if (operation == ImGuizmo::SCALE)
			op = ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y;

		ImGuizmo::SetOrthographic(true);
		ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(model), nullptr, snap);
		ImGuizmo::SetOrthographic(false);
		if (!ImGuizmo::IsUsing())
			return;

		// 拆回位置 / 旋转 / 缩放；只写当前操作改的那一项，免得浮点误差把别的分量慢慢带偏
		glm::vec2 newPosition = position;
		float newRotation = rotation;
		glm::vec2 newScale = scale;
		const glm::vec2 ex(model[0]);
		const glm::vec2 ey(model[1]);
		if (operation == ImGuizmo::ROTATE || operation == ImGuizmo::SCALE)
		{
			// x 缩放为负（镜像）时，x 轴方向和旋转方向相反
			const float lengthX = glm::length(ex);
			if (lengthX < 1e-6f)
				return;
			const glm::vec2 dir = ex / (scale.x < 0.0f ? -lengthX : lengthX);
			if (operation == ImGuizmo::ROTATE)
			{
				const float angle = glm::degrees(std::atan2(dir.y, dir.x));
				newRotation = rotation + std::remainder(angle - rotation, 360.0f);
			}
			else
			{
				newScale.x = glm::dot(ex, dir);
				newScale.y = glm::dot(ey, glm::vec2(-dir.y, dir.x));
			}
		}
		else
		{
			newPosition = glm::vec2(model[3]);
		}

		world->SubmitCommand([entity, newPosition, newRotation, newScale]() mutable
			{
				if (auto* t = entity.tryGetComponent<Transform2D>())
				{
					t->position = newPosition;
					t->rotation = newRotation;
					t->scale = newScale;
				}
			});
	}

	// 找 code.cmd：先查 PATH，再查两个常见安装位置。
	std::string FindVSCodeCommand()
	{
		char buffer[MAX_PATH] = {};
		if (::SearchPathA(nullptr, "code.cmd", nullptr, MAX_PATH, buffer, nullptr) > 0)
			return std::string(buffer);

		char localAppData[MAX_PATH] = {};
		const DWORD localLength = ::GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
		if (localLength > 0 && localLength < MAX_PATH)
		{
			const std::string candidate = std::string(localAppData) + "\\Programs\\Microsoft VS Code\\bin\\code.cmd";
			if (::GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				return candidate;
		}

		char programFiles[MAX_PATH] = {};
		const DWORD programLength = ::GetEnvironmentVariableA("ProgramFiles", programFiles, MAX_PATH);
		if (programLength > 0 && programLength < MAX_PATH)
		{
			const std::string candidate = std::string(programFiles) + "\\Microsoft VS Code\\bin\\code.cmd";
			if (::GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				return candidate;
		}

		return {};
	}

	bool RunDetached(const std::string& commandLine)
	{
		if (commandLine.empty())
			return false;

		std::vector<char> buffer(commandLine.begin(), commandLine.end());
		buffer.push_back('\0');

		STARTUPINFOA startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo{};

		if (!::CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &startupInfo, &processInfo))
			return false;

		::CloseHandle(processInfo.hThread);
		::CloseHandle(processInfo.hProcess);
		return true;
	}

	bool OpenWithVSCode(const std::string& filePath)
	{
		if (filePath.empty())
			return false;
		const std::string codeCommand = FindVSCodeCommand();
		if (codeCommand.empty())
			return false;

		// .cmd 必须经 cmd.exe 启动；路径里有空格，所以整体再加一层引号。
		const std::string commandLine = "cmd /c \"\"" + codeCommand + "\" \"" + filePath + "\"\"";
		return RunDetached(commandLine);
	}

	bool OpenWithNotePad(const std::string& filePath)
	{
		if (filePath.empty())
			return false;
		return RunDetached("notepad.exe \"" + filePath + "\"");
	}

	bool HasShellAssociation(const std::string& filePath)
	{
		const std::string extension = std::filesystem::path(filePath).extension().string();
		if (extension.empty())
			return false;

		const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, extension.c_str(), -1, nullptr, 0);
		if (wideLength <= 0)
			return false;

		std::wstring wideExtension(static_cast<size_t>(wideLength), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, extension.c_str(), -1, wideExtension.data(), wideLength);
		while (!wideExtension.empty() && wideExtension.back() == L'\0')
			wideExtension.pop_back();

		DWORD length = 0;
		HRESULT hr = ::AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, wideExtension.c_str(), nullptr, nullptr, &length);
		if (hr != S_FALSE || length <= 1)
			return false;

		std::wstring executable(static_cast<size_t>(length), L'\0');
		const HRESULT result = ::AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, wideExtension.c_str(), nullptr, executable.data(), &length);
		if (FAILED(result) || length <= 1)
			return false;

		if (_wcsicmp(std::filesystem::path(executable).filename().c_str(), L"OpenWith.exe") == 0)
			return false;

		return true;
	}

	bool OpenWithSystemDefault(const std::string& filePath)
	{
		if (filePath.empty())
			return false;

		const auto result = reinterpret_cast<INT_PTR>(
			::ShellExecuteA(nullptr, "open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		return result > 32;
	}

	bool IsCodeEditableAsset(AssetType type)
	{
		return type == AssetType::Script || type == AssetType::UiDocument || type == AssetType::UiStyle;
	}

	bool IsCodeEditableExtension(const std::string& extension)
	{
		std::string lower = extension;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lower == ".lua" || lower == ".ui" || lower == ".uistyle";
	}

	// 文件夹的"资产路径"：相对工程目录、正斜杠、UTF-8，例如 "UI/Textures"。
	// 文件用的是资产库里登记的 AssetMeta::path（见 DrawFile 的右键菜单）。不在工程目录里就返回空
	std::string ToProjectAssetPath(const fs::path& fullPath, const std::string& projectFolder)
	{
		if (projectFolder.empty() || fullPath.empty())
			return {};
		std::error_code ec;
		const fs::path root = fs::weakly_canonical(fs::path(projectFolder), ec);
		if (ec)
			return {};
		const fs::path relative = fullPath.lexically_relative(root);
		if (relative.empty() || relative == "." || *relative.begin() == "..")
			return {};
		const std::u8string u8 = relative.generic_u8string();
		return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
	}
}


ImGuizmo::OPERATION ImguiLayout::mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
ImGuizmo::MODE ImguiLayout::mCurrentGizmoMode = ImGuizmo::WORLD;
bool ImguiLayout::useSnap = false;
float ImguiLayout::snap[3] = { 0.1f, 0.1f, 0.1f };
Entity ImguiLayout::selectedEntity = Entity();

char ImguiLayout::m_newProjectPathBuffer[1000];
char ImguiLayout::m_newProjectName[1000];

std::string ImguiLayout::m_pendingAction;

char ImguiLayout::m_wantOpenProjectPathBuffer[1000];

bool ImguiLayout::s_openSaveConfirmPopup = false;
bool ImguiLayout::s_openNewProjectPopup = false;
bool ImguiLayout::s_openStopPlayConfirmPopup = false;

std::string ImguiLayout::m_selectedFolder;

AssetImportPopup ImguiLayout::s_assetImportPopup;
AssetGUID ImguiLayout::m_currentSceneAssetGuid;
static AssetGUID s_materialEditorGuid;
static bool s_materialEditorOpen = false;
static bool s_editComponentRemoveArmed = false;
struct MaterialOrigin
{
	AssetGUID guid;
	MaterialProperties properties;
	std::map<TextureType, AssetGUID> textures;
	bool valid = false;
};
static MaterialOrigin s_materialOrigin;
static std::string s_sceneSaveErrorMessage;
enum class CreateAssetKind { None, Material, Scene, Script };
static CreateAssetKind s_createAssetKind = CreateAssetKind::None;
static bool s_showCreateAssetPopup = false;
static bool s_requestCreateAssetPopup = false;
static char s_createAssetName[256] = {};
static std::string s_createAssetParentPath;
static std::string s_createAssetError;

static bool OpenProjectAndLoadDefaultScene(const std::string& filePath, ProjectManager* projectManager, EditorApp* editorApp)
{
	if (!projectManager || !editorApp || !projectManager->OpenProject(filePath))
		return false;

	ImguiLayout::m_currentSceneAssetGuid.clear();
	ImguiLayout::selectedEntity = Entity();
	// 场景由引擎打开：资产库定位、GUID 解析、反序列化都在引擎里，编辑器只提供
	// 自己的相机实体与视角恢复。工程没有默认场景（或打不开）时才退回内置空场景。
	const AssetGUID defaultSceneGUID = projectManager->GetDefaultSceneGUID();
	if (!defaultSceneGUID.empty() && editorApp->Scene().LoadSceneAsset(defaultSceneGUID))
	{
		ImguiLayout::m_currentSceneAssetGuid = defaultSceneGUID;
		return true;
	}

	editorApp->Scene().InitDefaultWorld();
	return true;
}

void ImguiLayout::OpenMaterialEditor(const AssetGUID& guid)
{
	if (s_materialEditorOpen && s_materialEditorGuid != guid)
	{
		auto currentAsset = EditorApp::Instance()->GetProjectManager()->LoadAsset(s_materialEditorGuid);
		auto currentMaterial = currentAsset ? std::dynamic_pointer_cast<MaterialAsset>(currentAsset) : nullptr;
		if (currentMaterial && s_materialOrigin.valid && s_materialOrigin.guid == s_materialEditorGuid)
		{
			currentMaterial->SetProperties(s_materialOrigin.properties);
			std::vector<TextureType> currentTextures;
			for (const auto& [type, textureGuid] : currentMaterial->GetTextureDependencies())
				currentTextures.push_back(type);
			for (TextureType type : currentTextures)
				currentMaterial->SetTextureDependency(type, {}, nullptr);
			for (const auto& [type, textureGuid] : s_materialOrigin.textures)
			{
				auto textureAsset = EditorApp::Instance()->GetProjectManager()->LoadAsset(textureGuid);
				auto texture = textureAsset ? textureAsset->AsTexture() : nullptr;
				if (texture)
					currentMaterial->SetTextureDependency(type, textureGuid, texture);
			}
		}
	}
	const bool isNewSession = !s_materialEditorOpen || s_materialEditorGuid != guid;
	s_materialEditorGuid = guid;
	s_materialEditorOpen = true;
	if (isNewSession)
		s_materialOrigin.valid = false;
}

bool ImguiLayout::s_pendingNeedSaveConfirm = true;

bool ImguiLayout::RequestProjectFlow(ProjectManager* projectManager, const std::string& action, bool needSaveConfirm)
{
	m_pendingAction = action;
	s_pendingNeedSaveConfirm = needSaveConfirm;

	auto editorApp = EditorApp::Instance();
	if (editorApp && editorApp->Scene().GetEditorState() != EditorWorldState::Editing) {
		s_openStopPlayConfirmPopup = true;
		return true;
	}

	if (needSaveConfirm && projectManager && projectManager->IsProjectOpen()) {
		s_openSaveConfirmPopup = true;
		return true;
	}

	m_pendingAction.clear();
	return false;
}

bool ImguiLayout::SaveCurrentScene(ProjectManager* projectManager)
{
	if (!projectManager || !projectManager->IsProjectOpen())
		return false;
	auto editorApp = EditorApp::Instance();
	auto world = editorApp ? editorApp->GetWorld() : nullptr;
	if (!world)
		return false;
	SceneEditor::RegisterSceneComponentCodecs();
	auto serializeCurrentWorld = [world](SceneCodec& scene)
		{
			auto serialize = [&scene](World& activeWorld)
				{
					const bool serialized = scene.Serialize(activeWorld, [](Entity entity) {
						if (auto follow = entity.tryGetComponent<CameraFollow>())
						{
							if (follow->target && follow->target.hasComponent<TagEditorCamera>())
								return false;
						}
						return !entity.hasComponent<TagEditorCamera>();
						});
					if (!serialized)
						return false;

					auto& settings = scene.GetSettings();
					settings.erase("editorFreeCamera");
					if (auto runtimeApp = EditorApp::Instance())
						runtimeApp->Runtime().CaptureSceneRenderOption(scene);
					for (Entity entity : activeWorld.getAllEntity())
					{
						if (!entity.hasComponents<CameraComponent, Transform, CameraFollow>())
							continue;

						auto& follow = entity.getComponent<CameraFollow>();
						Entity freeEntity = follow.target;
						if (!freeEntity || !freeEntity.hasComponents<TagFreeCamera, TagCurrentControl, Transform, PlayerInput, Controller, TagEditorCamera>())
							continue;

						const auto& camera = entity.getComponent<CameraComponent>();
						const auto& cameraTransform = entity.getComponent<Transform>();
						const auto& controller = freeEntity.getComponent<Controller>();
						const auto& freeCamera = freeEntity.getComponent<TagFreeCamera>();
						const auto finiteVec3 = [](const glm::vec3& value) {
							return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
							};
						const auto finiteQuat = [](const glm::quat& value) {
							return std::isfinite(value.w) && std::isfinite(value.x)
								&& std::isfinite(value.y) && std::isfinite(value.z);
							};
						if (!finiteVec3(cameraTransform.position) || !finiteVec3(cameraTransform.scale)
							|| !finiteQuat(cameraTransform.rotation)
							|| !finiteVec3(follow.offset)
							|| !std::isfinite(controller.yaw) || !std::isfinite(controller.pitch)
							|| !std::isfinite(freeCamera.velocity)
							|| !std::isfinite(camera.fov) || !std::isfinite(camera.nearPlane)
							|| !std::isfinite(camera.farPlane))
							return false;

						settings["editorFreeCamera"] = {
							{"yaw", controller.yaw},
							{"pitch", controller.pitch},
							{"velocity", freeCamera.velocity},
							{"cameraTransform", {
								{"position", { cameraTransform.position.x, cameraTransform.position.y, cameraTransform.position.z }},
								{"rotation", { cameraTransform.rotation.w, cameraTransform.rotation.x, cameraTransform.rotation.y, cameraTransform.rotation.z }},
								{"scale", { cameraTransform.scale.x, cameraTransform.scale.y, cameraTransform.scale.z }}
							}},
							{"fov", camera.fov},
							{"nearPlane", camera.nearPlane},
							{"farPlane", camera.farPlane},
							{"offset", { follow.offset.x, follow.offset.y, follow.offset.z }}
						};
						break;
					}
					return true;
				};

			try
			{
				if (!world->isRunning())
					return serialize(*world);

				bool result = false;
				world->SubmitCommand([&](World& activeWorld) {
					result = serialize(activeWorld);
					}).get();
				return result;
			}
			catch (const std::exception&)
			{
				return false;
			}
		};

	if (!m_currentSceneAssetGuid.empty())
	{
		auto database = projectManager->GetDatabase();
		auto meta = database ? database->GetAssetMetaByGuid(m_currentSceneAssetGuid) : nullptr;
		if (meta && meta->type == AssetType::Scene)
		{
			if (auto asset = projectManager->LoadAsset(m_currentSceneAssetGuid))
			{
				if (auto scene = asset->AsScene())
				{
					if (!serializeCurrentWorld(*scene))
					{
						s_sceneSaveErrorMessage = "无法序列化当前场景。";
						return false;
					}

					bool result = projectManager->SaveAsset(meta->guid);

					if (!result)
						s_sceneSaveErrorMessage = "无法写入当前场景资产文件。";

					return result;
				}
			}
		}
	}
	else
	{
		std::string filePath;
		if (!FileIO::SaveOneFile(
			filePath,
			"Untitled.scene",
			"scene",
			{ {"场景文件", "scene"} },
			projectManager->GetProjectFolderFullPath())
			)
			return false;

		SceneCodec scene;
		scene.SetName(fs::path(filePath).stem().string());
		if (!serializeCurrentWorld(scene))
		{
			s_sceneSaveErrorMessage = "无法序列化当前场景。";
			return false;
		}
		AssetMeta meta;
		if (!projectManager->CreateSceneAsset(filePath, scene, &meta))
		{
			s_sceneSaveErrorMessage = "保存失败。请确认所选路径位于项目根目录内，且未与其他类型的资产冲突。";
			return false;
		}

		m_currentSceneAssetGuid = meta.guid;
		return true;
	}

}

// 新建文件夹
bool        m_showNewFolderPopup = false;
char        m_newFolderBuffer[256] = {};
std::string m_newFolderParentPath;

// 重命名
bool        m_showRenamePopup = false;
char        m_renameBuffer[256] = {};
std::string m_renameTarget;
bool        m_renameIsFolder = false;


static bool WantToAddDirLight = false;
static bool WantToAddPointLight = false;
static bool WantToAddSpotLight = false;
static bool WantToAddCube = false;
static bool WantToAddSphere = false;
static bool WantToAddEmptyEntity = false;
static bool WantToAddDDGIVolume = false;

extern VkDevice g_Device;


const char* GetOpName(ImGuizmo::OPERATION operation)
{
	// 注意：OPERATION 是位标志，需要按位检查
	if (operation == ImGuizmo::TRANSLATE)
		return "平移";
	if (operation == ImGuizmo::ROTATE)
		return "旋转";
	if (operation == ImGuizmo::SCALE)
		return "缩放";
	if (operation == ImGuizmo::BOUNDS)
		return "Bounds";
	if (operation == ImGuizmo::UNIVERSAL)
		return "Universal";

	std::string combined;
	if (operation & ImGuizmo::TRANSLATE) combined += "T";
	if (operation & ImGuizmo::ROTATE) combined += "R";
	if (operation & ImGuizmo::SCALE) combined += "S";

	static std::string result;
	result = combined.empty() ? "Unknown" : combined;
	return result.c_str();
}

const char* GetModeName(ImGuizmo::MODE mode)
{
	if (mode == ImGuizmo::LOCAL)
		return "Local";
	if (mode == ImGuizmo::WORLD)
		return "World";

	return "Unknown";
}

static btVector3 GlmToBullet(const glm::vec3& v) {
	return btVector3(v.x, v.y, v.z);
}

static btQuaternion GlmToBullet(const glm::quat& q) {
	return btQuaternion(q.x, q.y, q.z, q.w);
}

void ImguiLayout::DrawFolderTree(const std::string& currentPathStr, const std::string& parentPathStr, const std::string& rootPathStr)
{
	fs::path currentPath(currentPathStr);
	fs::path parentPath(parentPathStr);
	fs::path rootPath(rootPathStr);

	if (!Tool::IsSubDirectory(currentPath, parentPath) || !Tool::IsSubDirectory(currentPath, rootPath))
		return;

	fs::path relativeParentPath = fs::relative(currentPath, parentPath);
	fs::path relativeRootPath = fs::relative(currentPath, rootPath);

	if (m_selectedFolder.empty())
		m_selectedFolder = ".";

	bool isSelected = (m_selectedFolder == relativeRootPath.string());

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (isSelected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	auto relativePathStr = relativeParentPath.string();
	if (relativeParentPath.string().empty() || relativeParentPath.string() == ".")
		relativePathStr = currentPath.filename().string();

	bool isSetOpen = Tool::IsSubDirectory(m_selectedFolder, relativeRootPath, rootPath) && m_selectedFolder != relativeRootPath.string();
	if (isSetOpen) ImGui::SetNextItemOpen(true);
	bool isExpanded = ImGui::TreeNodeEx(relativePathStr.c_str(), flags);

	if (ImGui::IsItemClicked()) {
		m_selectedFolder = relativeRootPath.string();
	}

	if (isSetOpen || isExpanded) {
		// 递归绘制子文件夹
		std::vector<std::string> folders;

		try {
			for (const auto& entry : fs::directory_iterator(currentPath)) {
				if (entry.is_directory()) {
					folders.push_back(entry.path().filename().string());
				}
			}
		}
		catch (...) {
			return;
		}

		std::sort(folders.begin(), folders.end());
		for (const auto& folder : folders) {
			std::filesystem::path childPath = currentPath / folder;
			DrawFolderTree(childPath.string(), currentPathStr, rootPathStr);
		}
		if (isExpanded)
			ImGui::TreePop();
	}
}

void ImguiLayout::DrawFolderContent(ProjectManager* projectManager, const std::string& currentPathStr)
{
	std::filesystem::path currentPath(currentPathStr);

	std::vector<std::string> folders;
	std::vector<std::string> files;

	try {
		for (const auto& entry : fs::directory_iterator(currentPath)) {
			std::string name = entry.path().filename().string();
			std::u8string utf8Name = entry.path().filename().u8string();
			std::string displayName(reinterpret_cast<const char*>(utf8Name.data()), utf8Name.size());
			if (entry.is_directory()) {
				folders.push_back(displayName);
			}
			else {
				files.push_back(displayName);
			}
		}
	}
	catch (...) {
		return;
	}

	std::unordered_map<std::string, AssetMeta> metas;
	if (Tool::IsSubDirectory(currentPath, projectManager->GetProjectFolderFullPath()))
	{
		fs::path relativeAssetFolderPath = fs::relative(currentPath, projectManager->GetProjectFolderFullPath());
		auto m = projectManager->GetAssetMetasInFolder(relativeAssetFolderPath.string());

		std::for_each(std::execution::seq, m.begin(), m.end(),
			[&](AssetMeta& meta) -> void {
				auto filename = fs::path(meta.path).filename().string();
				metas[filename] = std::move(meta);
			});
	}

	std::sort(folders.begin(), folders.end());
	std::sort(files.begin(), files.end(),
		[&](const std::string& file_a, const std::string& file_b) -> bool
		{
			auto it1 = metas.find(file_a);
			auto it2 = metas.find(file_b);
			bool isAsset_a = it1 != metas.end();
			bool isAsset_b = it2 != metas.end();

			if (isAsset_a != isAsset_b)
			{
				if (isAsset_a) return true;
				if (isAsset_b) return false;
			}
			else if (!isAsset_a)
			{
				return file_a < file_b;
			}
			else
			{
				return it1->second.type > it2->second.type;
			}
		}
	);

	struct Item {
		bool isFolder;
		bool isAssetFile;
		AssetMeta meta;
		AssetPath name;
	};
	std::vector<Item> items;
	items.resize(folders.size() + files.size());

	std::for_each(std::execution::par,
		std::views::enumerate(folders).begin(),
		std::views::enumerate(folders).end(),
		[&](const auto& pair) {
			const auto& [idx, folder] = pair;
			items[idx] = { true, false, {}, std::move(folder) };
		});

	std::for_each(std::execution::par,
		std::views::enumerate(files).begin(),
		std::views::enumerate(files).end(),
		[&](const auto& pair) {
			const auto& [idx, file] = pair;
			bool isAsset = metas.find(file) != metas.end();
			items[folders.size() + idx] = { false, isAsset, isAsset ? metas[file] : AssetMeta{}, std::move(file) };
		});

	// ---- 顶部：筛选 + 搜索。只在当前文件夹显示的项里过滤（不进子文件夹），筛选和搜索取交集 ----
	auto& browserFilter = BrowserFilter();
	const size_t totalItemCount = items.size();
	{
		const auto terms = SplitSearchTerms(browserFilter.search);
		std::erase_if(items, [&](const Item& item)
			{
				return !PassBrowserFilter(browserFilter, item.isFolder, item.isAssetFile, item.meta.type)
					|| !MatchSearchTerms(item.name, terms);
			});
	}
	DrawBrowserFilterBar(browserFilter, items.size(), totalItemCount);
	ImGui::Separator();

	// 下面的格子放进子窗口：滚动时顶部的筛选栏不跟着走
	ImGui::BeginChild("##FolderItems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

	// 网格布局
	float iconSize = 80.0f;
	float padding = 8.f;
	float textHeight = ImGui::GetFontSize() * 2;
	float windowWidth = ImGui::GetContentRegionAvail().x;
	float rowHeight = iconSize + textHeight + padding;

	int columns = std::max(1, (int)(windowWidth / (iconSize + padding)));

	int totalItems = items.size();
	int totalRows = (totalItems + columns - 1) / columns;


	ImGui::Columns(columns, nullptr, false);

	auto* iconMgr = IconManager::Get();

	bool anyItemHovered = false;

	// ---- 用于删除/重命名等操作的延迟处理（避免在遍历时修改目录） ----
	std::string pendingDeletePath;   // 待删除的完整路径
	AssetGUID   pendingDeleteGuid;   // 若是资产，记录 guid
	bool        pendingDeleteIsAsset = false;
	std::string pendingDeleteName;   // 用于日志

	auto DrawFolder = [&](const Item& item) -> void
		{
			ImGui::PushID(item.name.c_str());

			ImVec2 iconPos = ImGui::GetCursorScreenPos();
			ImVec2 iconRect(iconSize, iconSize);

			ImGui::InvisibleButton("##icon_area", iconRect);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImTextureID folderIcon = iconMgr->GetFolderIcon();
			if (folderIcon != 0) {
				drawList->AddImage(
					folderIcon,
					ImVec2(iconPos.x + iconSize * 0.1, iconPos.y + iconSize * 0.1),
					ImVec2(iconPos.x + iconSize * 0.9, iconPos.y + iconSize * 0.9)
				);
			}

			bool isHovered = ImGui::IsItemHovered();
			bool isDbClicked = isHovered && ImGui::IsMouseDoubleClicked(0);
			bool isRightClicked = isHovered && ImGui::IsMouseClicked(1);

			if (isHovered)
			{
				anyItemHovered = true;
				drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
					IM_COL32(70, 130, 200, 150));
			}

			auto fullFolderPath = fs::weakly_canonical(currentPath / item.name);

			if (isDbClicked)
			{
				fs::path relativeRootPath = fs::relative(fullFolderPath, fs::path(projectManager->GetProjectFolderFullPath()));
				m_selectedFolder = relativeRootPath.string();
			}

			if (isRightClicked) {
			}

			if (ImGui::BeginPopupContextItem("folder_context_menu"))
			{
				if (ImGui::MenuItem("打开")) {
					fs::path relativeRootPath = fs::relative(fullFolderPath, fs::path(projectManager->GetProjectFolderFullPath()));
					m_selectedFolder = relativeRootPath.string();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("复制路径")) {
					ImGui::SetClipboardText(fullFolderPath.string().c_str());
				}
				{
					const std::string assetPath = projectManager->IsProjectOpen() ? ToProjectAssetPath(fullFolderPath, projectManager->GetProjectFolderFullPath()) : std::string();
					if (ImGui::MenuItem("复制资产路径", nullptr, false, !assetPath.empty()))
						ImGui::SetClipboardText(assetPath.c_str());
					if (!assetPath.empty() && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", assetPath.c_str());
				}
				if (ImGui::MenuItem("复制文件夹名")) {
					ImGui::SetClipboardText(item.name.c_str());
				}

				ImGui::Separator();

				if (ImGui::MenuItem("重命名")) {
					m_renameTarget = fullFolderPath.string();
					m_renameIsFolder = true;
					strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
					m_showRenamePopup = true;
				}

				if (ImGui::MenuItem("删除文件夹")) {
					// 延迟删除，避免在遍历/绘制时修改目录
					pendingDeletePath = fullFolderPath.string();
					pendingDeleteIsAsset = false;
					pendingDeleteGuid.clear();
					pendingDeleteName = item.name;
				}

				ImGui::EndPopup();
			}

			ImVec2 textPos = ImGui::GetCursorScreenPos();
			ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str(), nullptr, false, iconSize);
			drawList->AddText(
				ImGui::GetFont(),
				ImGui::GetFontSize(),
				textPos,
				ImGui::GetColorU32(ImGuiCol_Text),
				item.name.c_str(),
				nullptr,
				iconSize,
				nullptr
			);
			ImGui::Dummy(ImVec2(iconSize, textSize.y));

			ImGui::NextColumn();
			ImGui::PopID();
		};

	auto DrawFile = [&](const Item& item) -> void {
		bool isAssetFile = item.isAssetFile;
		AssetMeta meta = item.meta;

		auto fullFilePath = fs::weakly_canonical(currentPath / item.name);

		ImGui::PushID(item.name.c_str());

		ImVec2 iconPos = ImGui::GetCursorScreenPos();
		ImVec2 iconRect(iconSize, iconSize);

		ImGui::InvisibleButton("##icon_area", iconRect);

		ImTextureID fileIcon = isAssetFile ?
			iconMgr->GetIconByAssetType(meta.type)
			: iconMgr->GetIconByFilePath(item.name);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		// 贴图 / 精灵 / 动图显示内容缩略图（动图会动）。缩略图是独立模块、后台生成，不加载资产；
		// 还没生成好、生成失败或关掉了缩略图时退回类型图标
		Render2D::SpriteFrame thumbnail;
		if (isAssetFile && AssetThumbnails::IsSupported(meta.type)
			&& ImGui::IsRectVisible(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize)))	// 只给视野里的格子请求
			thumbnail = AssetThumbnails::Get(projectManager, meta, Sprite2DEditor::EditorTimeMs());
		if (thumbnail.IsValid())
		{
			Sprite2DEditor::DrawFrameInRect(drawList, thumbnail,
				ImVec2(iconPos.x + iconSize * 0.1f, iconPos.y + iconSize * 0.1f),
				ImVec2(iconPos.x + iconSize * 0.9f, iconPos.y + iconSize * 0.9f));
		}
		else if (fileIcon != 0) {
			drawList->AddImage(
				fileIcon,
				ImVec2(iconPos.x + iconSize * 0.1, iconPos.y + iconSize * 0.1),
				ImVec2(iconPos.x + iconSize * 0.9, iconPos.y + iconSize * 0.9)
			);
		}

		bool isHovered = ImGui::IsItemHovered();
		bool isDbClicked = isHovered && ImGui::IsMouseDoubleClicked(0);
		bool isRightClicked = isHovered && ImGui::IsMouseClicked(1);

		if (isHovered)
		{
			anyItemHovered = true;
			drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
				IM_COL32(70, 130, 200, 150));
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				if (isAssetFile)
				{
					AssetGUID str = meta.guid;
					ImGui::SetDragDropPayload("ASSET_GUID", str.c_str(), str.size() + 1);
				}
				else
				{
					std::string str = fullFilePath.string();
					ImGui::SetDragDropPayload("ASSET_FILE", str.c_str(), str.size() + 1);
				}

				ImGui::Image(fileIcon, ImVec2(iconSize * 0.8, iconSize * 0.8));
				ImGui::Text("%s", item.name.c_str());
				ImGui::EndDragDropSource();
			}
		}

		static auto OpenFile = [](AssetMeta* meta, const fs::path& fullFilePath)
			{
				bool isAssetFile = meta != nullptr;
				if (isAssetFile && meta->type == AssetType::Material)
				{
					ImguiLayout::OpenMaterialEditor(meta->guid);
					return;
				}
				if (isAssetFile && meta->type == AssetType::Sprite)
				{
					Sprite2DEditor::OpenSpriteEditor(meta->guid);
					return;
				}
				if (isAssetFile && meta->type == AssetType::AnimatedImage)
				{
					Sprite2DEditor::OpenAnimatedImageViewer(meta->guid);
					return;
				}

				if (fullFilePath.empty())
					return;

				// .ui / .uistyle / .lua：用内置文本编辑器打开（VS Code、记事本、系统默认程序在右键菜单里）
				// 调用方总是传 &meta（不是资产时 type 为 Unknown），所以按类型或扩展名任一命中都算
				const std::string itemExtension = fullFilePath.extension().string();
				const bool codeEditable = (isAssetFile && IsCodeEditableAsset(meta->type)) || IsCodeEditableExtension(itemExtension);
				if (codeEditable)
				{
					TextAssetEditor::Open(fullFilePath);
					return;
				}

				auto strPath = fullFilePath.string();
				OpenWithSystemDefault(strPath);
			};

		if (isDbClicked)
		{
			OpenFile(&meta, fullFilePath);
		}

		if (isRightClicked) {
		}

		if (ImGui::BeginPopupContextItem("file_context_menu"))
		{
			if (!fullFilePath.empty())
			{
				const std::string itemExtension = fullFilePath.extension().string();
				const bool codeEditable = (isAssetFile && IsCodeEditableAsset(meta.type)) || IsCodeEditableExtension(itemExtension);

				// 顺序：内置打开 > 指定程序（VS Code、记事本） > 系统默认程序
				if (ImGui::MenuItem("打开"))
				{
					OpenFile(&meta, fullFilePath);
				}
				if (codeEditable)
				{
					if (ImGui::MenuItem("用 VS Code 打开"))
					{
						OpenWithVSCode(fullFilePath.string());
					}
					if (ImGui::MenuItem("用记事本打开"))
					{
						OpenWithNotePad(fullFilePath.string());
					}
					if (ImGui::MenuItem("用系统默认程序打开", nullptr, false, HasShellAssociation(fullFilePath.string())))
					{
						OpenWithSystemDefault(fullFilePath.string());
					}
				}
			}
			else if (ImGui::MenuItem("打开"))
			{
				OpenFile(&meta, fullFilePath);
			}

			if (isAssetFile && meta.type == AssetType::Texture && ImGui::MenuItem("生成精灵"))
				RequestSpriteFromTexture(meta.guid, projectManager);
			if (isAssetFile && meta.type == AssetType::Sprite && ImGui::MenuItem("生成贴图"))
				RequestTextureFromSprite(meta.guid, projectManager);

			ImGui::Separator();

			if (ImGui::MenuItem("复制路径")) {
				ImGui::SetClipboardText(fullFilePath.string().c_str());
			}
			{
				// 资产路径 = 资产库里登记的 AssetMeta::path（图片资产就是它的 .meta），统一成正斜杠、UTF-8。
				// .ui / .uistyle 里写这个，引擎按资产库查，导入配置、Sprite 区域都生效
				std::string assetPath;
				if (isAssetFile && !meta.path.empty())
				{
					const std::u8string u8 = fs::path(meta.path).generic_u8string();
					assetPath.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
				}
				if (ImGui::MenuItem("复制资产路径", nullptr, false, !assetPath.empty()))
					ImGui::SetClipboardText(assetPath.c_str());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					if (!assetPath.empty())
						ImGui::SetTooltip("%s\n（.ui / .uistyle 里引用图片、样式时直接写这个）", assetPath.c_str());
					else
						ImGui::SetTooltip("还不是资产（没有导入），没有资产路径");
				}
			}
			// 只有登记过的资产才有 GUID。.ui 里引用图片写 GUID，图片重命名、移动后也不会失效；样式引用只认路径
			if (isAssetFile && !meta.guid.empty())
			{
				if (ImGui::MenuItem("复制 GUID"))
					ImGui::SetClipboardText(meta.guid.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n（.ui 里引用图片时可以写 GUID，重命名、移动后也不会失效；样式引用只认路径）", meta.guid.c_str());
			}
			if (ImGui::MenuItem("复制文件名")) {
				ImGui::SetClipboardText(item.name.c_str());
			}

			ImGui::Separator();

			if (ImGui::MenuItem("重命名")) {
				m_renameTarget = fullFilePath.string();
				m_renameIsFolder = false;
				strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
				m_showRenamePopup = true;
			}

			std::string deleteStr = isAssetFile ? "删除资产" : "删除文件";
			if (ImGui::MenuItem(deleteStr.c_str()))
			{
				pendingDeletePath = fullFilePath.string();
				pendingDeleteIsAsset = isAssetFile;
				pendingDeleteGuid = isAssetFile ? meta.guid : AssetGUID{};
				pendingDeleteName = item.name;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("属性")) { /*ShowFileProperties*/ }

			ImGui::EndPopup();
		}

		ImVec2 textPos = ImGui::GetCursorScreenPos();
		ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str(), nullptr, false, iconSize);
		drawList->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize(),
			textPos,
			ImGui::GetColorU32(ImGuiCol_Text),
			item.name.c_str(),
			nullptr,
			iconSize,
			nullptr
		);
		ImGui::Dummy(ImVec2(iconSize, textSize.y));

		ImGui::NextColumn();
		ImGui::PopID();
		};


	// ==========================================================
	// 绘制网格内容
	// ==========================================================
	ImGuiListClipper clipper;
	clipper.Begin(totalRows, rowHeight);
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
		{
			int startIndex = row * columns;
			int endIndex = std::min(startIndex + columns, totalItems);

			for (int i = startIndex; i < endIndex; i++)
			{
				const auto& item = items[i];
				if (item.isFolder)
					DrawFolder(item);
				else
					DrawFile(item);
			}
		}
	}

	ImGui::Columns(1);

	bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

	if (windowHovered && !anyItemHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("blank_context_menu");
	}

	if (ImGui::BeginPopup("blank_context_menu"))
	{
		if (ImGui::BeginMenu("创建资产"))
		{
			auto beginCreateAsset = [&](CreateAssetKind kind)
				{
					s_createAssetKind = kind;
					s_createAssetParentPath = currentPath.string();
					s_createAssetName[0] = '\0';
					s_createAssetError.clear();
					s_requestCreateAssetPopup = true;
				};
			if (ImGui::MenuItem("材质"))
				beginCreateAsset(CreateAssetKind::Material);
			if (ImGui::MenuItem("场景"))
				beginCreateAsset(CreateAssetKind::Scene);
			if (ImGui::MenuItem("脚本"))
				beginCreateAsset(CreateAssetKind::Script);
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("新建文件夹"))
		{
			m_showNewFolderPopup = true;
			m_newFolderBuffer[0] = '\0';
			m_newFolderParentPath = currentPath.string();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("刷新")) {
			// 下一帧自动重新读取
		}

		ImGui::EndPopup();
	}

	if (s_requestCreateAssetPopup)
	{
		ImGui::OpenPopup("创建资产");
		s_requestCreateAssetPopup = false;
		s_showCreateAssetPopup = true;
	}
	if (ImGui::BeginPopupModal("创建资产", &s_showCreateAssetPopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const char* assetTypeName = s_createAssetKind == CreateAssetKind::Material ? "材质"
			: s_createAssetKind == CreateAssetKind::Scene ? "场景" : "脚本";
		const char* extension = s_createAssetKind == CreateAssetKind::Scene ? ".scene"
			: s_createAssetKind == CreateAssetKind::Script ? ".lua" : "";
		ImGui::Text("创建%s", assetTypeName);
		ImGui::Text("目录: %s", s_createAssetParentPath.c_str());
		ImGui::InputText("名称", s_createAssetName, sizeof(s_createAssetName));
		if (extension[0] != '\0')
			ImGui::SameLine(), ImGui::Text("%s", extension);

		std::string name = s_createAssetName;
		const bool validName = !name.empty() && name != "." && name != ".."
			&& fs::path(name).filename().string() == name
			&& name.find_first_of("/\\") == std::string::npos;
		fs::path targetPath = fs::path(s_createAssetParentPath) / (name + extension);
		bool canCreate = validName && !fs::exists(targetPath);
		if (validName && fs::exists(targetPath))
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "同名文件已存在");
		if (validName && s_createAssetKind != CreateAssetKind::Material)
		{
			const auto relativePath = fs::relative(targetPath, projectManager->GetProjectFolderFullPath()).string();
			auto database = projectManager->GetDatabase();
			if (database && database->GetAssetMetaByPath(relativePath))
			{
				canCreate = false;
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "资产数据库中已存在同路径资产");
			}
		}
		if (!s_createAssetError.empty())
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "%s", s_createAssetError.c_str());

		if (ImGui::Button("创建", ImVec2(120, 0)) && canCreate)
		{
			bool created = false;
			std::vector<AssetMeta> createdMetas;
			try
			{
				if (s_createAssetKind == CreateAssetKind::Material)
				{
					created = projectManager->ImportMaterialAsset(targetPath.string(), MaterialProperties{}, {}, &createdMetas, true);
				}
				else if (s_createAssetKind == CreateAssetKind::Scene)
				{
					auto scene = SceneEditor::CreateDefaultSceneCodec();
					if (!scene)
						s_createAssetError = "无法生成引擎默认世界场景。";
					else
					{
						scene->SetName(name);
						if (auto runtimeApp = EditorApp::Instance())
							runtimeApp->Runtime().CaptureSceneRenderOption(*scene);
						AssetMeta sceneMeta;
						created = projectManager->CreateSceneAsset(targetPath.string(), *scene, &sceneMeta);
						if (created)
							createdMetas.push_back(sceneMeta);
					}
				}
				else if (s_createAssetKind == CreateAssetKind::Script)
				{
					std::ofstream scriptFile(targetPath, std::ios::binary | std::ios::out);
					if (scriptFile)
					{
						scriptFile.close();
						created = projectManager->ImportScriptAsset(targetPath.string(), targetPath.string(), &createdMetas, true);
						if (!created)
							fs::remove(targetPath);
					}
				}
			}
			catch (const std::exception& exception)
			{
				s_createAssetError = exception.what();
			}

			if (created)
			{
				s_showCreateAssetPopup = false;
				ImGui::CloseCurrentPopup();
			}
			else if (s_createAssetError.empty())
			{
				s_createAssetError = "创建资产失败。";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(120, 0)))
		{
			s_showCreateAssetPopup = false;
			s_createAssetError.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// ==========================================================
	// 新建文件夹弹窗
	// ==========================================================
	if (m_showNewFolderPopup)
	{
		ImGui::OpenPopup("新建文件夹");
	}
	if (ImGui::BeginPopupModal("新建文件夹", &m_showNewFolderPopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("父目录: %s", m_newFolderParentPath.c_str());
		ImGui::Separator();

		ImGui::InputText("文件夹名", m_newFolderBuffer, sizeof(m_newFolderBuffer));

		bool valid = m_newFolderBuffer[0] != '\0';

		if (valid) {
			fs::path newPath = fs::path(m_newFolderParentPath) / m_newFolderBuffer;
			if (fs::exists(newPath)) {
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "已存在同名文件/文件夹");
				valid = false;
			}
		}

		if (valid && ImGui::Button("创建", ImVec2(120, 0)))
		{
			try {
				fs::create_directory(fs::path(m_newFolderParentPath) / m_newFolderBuffer);
				m_showNewFolderPopup = false;
				ImGui::CloseCurrentPopup();
			}
			catch (const fs::filesystem_error& e) {
				// 可以弹一个错误提示
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(120, 0))) {
			m_showNewFolderPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// ==========================================================
	// 重命名弹窗
	// ==========================================================
	if (m_showRenamePopup)
	{
		ImGui::OpenPopup("重命名");
	}
	if (ImGui::BeginPopupModal("重命名", &m_showRenamePopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("目标: %s", m_renameTarget.c_str());
		ImGui::Separator();

		ImGui::InputText("新名称", m_renameBuffer, sizeof(m_renameBuffer));

		bool valid = m_renameBuffer[0] != '\0';

		if (valid && ImGui::Button("确定", ImVec2(120, 0)))
		{
			try {
				fs::path oldPath(m_renameTarget);
				fs::path newPath = oldPath.parent_path() / m_renameBuffer;
				if (!fs::exists(newPath)) {
					fs::rename(oldPath, newPath);
				}
				m_showRenamePopup = false;
				ImGui::CloseCurrentPopup();
			}
			catch (const fs::filesystem_error&) {
				// 错误处理
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(120, 0))) {
			m_showRenamePopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// ==========================================================
	// 延迟删除处理（遍历结束后执行）
	// ==========================================================
	if (!pendingDeletePath.empty())
	{
		try {
			auto path = fs::path(pendingDeletePath);
			if (fs::exists(path))
			{
				if (fs::is_directory(path))
				{
					projectManager->DeleteAssetsInFolder(path);
					fs::remove_all(path);
				}
				else
				{
					fs::remove_all(path);
					if (pendingDeleteIsAsset && !pendingDeleteGuid.empty())
						projectManager->DeleteAsset(pendingDeleteGuid);
				}
			}
		}
		catch (const fs::filesystem_error& e) {
			// 可以记录日志 / 弹窗
		}
	}

	ImGui::EndChild();	// ##FolderItems
}

void ImguiLayout::DrawMainMenu(GLFWwindow* window, EditorApp* editorApp, ProjectManager* projectManager)
{

	auto NewProjectPopup = [&]()->void {
		ImGui::OpenPopup("NewProjectPopup");
		if (ImGui::BeginPopupModal("NewProjectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("创建新项目");
			ImGui::Separator();

			ImGui::Text("项目路径:");
			ImGui::SameLine();
			ImGui::InputText("##ProjectPath", m_newProjectPathBuffer, sizeof(m_newProjectPathBuffer));
			ImGui::SameLine();
			if (ImGui::Button("浏览...")) {
				std::string folderPath;
				if (FileIO::SelectFolder(folderPath))
				{
					memset(m_newProjectPathBuffer, 0, sizeof(m_newProjectPathBuffer));
					strcpy(m_newProjectPathBuffer, folderPath.c_str());
				}
			}

			ImGui::Text("项目名称:");
			ImGui::InputText("##ProjectName", m_newProjectName, sizeof(m_newProjectName));

			if (ImGui::Button("创建项目")) {
				if (strlen(m_newProjectName) > 0 && strlen(m_newProjectPathBuffer) > 0) {
					projectManager->CreateProject(m_newProjectPathBuffer, m_newProjectName);
					memset(m_newProjectName, 0, sizeof(m_newProjectName));
					ImGui::CloseCurrentPopup();
				}
				s_openNewProjectPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("取消")) {
				memset(m_newProjectName, 0, sizeof(m_newProjectName));
				ImGui::CloseCurrentPopup();
				s_openNewProjectPopup = false;
			}
			ImGui::EndPopup();
		}
		};

	auto ExecutePendingAction = [&](GLFWwindow* window)-> void {
		if (m_pendingAction == "new") {
			s_openNewProjectPopup = true;
		}
		else if (m_pendingAction == "open") {
			std::string filePath;
			if (FileIO::OpenOneFile(filePath, { { "project",".project" } }))
			{
				OpenProjectAndLoadDefaultScene(filePath, projectManager, editorApp);
				m_selectedFolder.clear();
			}
		}
		else if (m_pendingAction == "exit") {
			glfwSetWindowShouldClose(window, true);
		}
		else if (m_pendingAction == "wantOpenProject") {
			std::string filePath(m_wantOpenProjectPathBuffer);
			if (!filePath.empty())
			{
				OpenProjectAndLoadDefaultScene(filePath, projectManager, editorApp);
				m_selectedFolder.clear();
			}
			memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
		}
		m_pendingAction = "";
		};

	auto IsWorldRunning = [&]()->bool {
		return editorApp && editorApp->Scene().GetEditorState() != EditorWorldState::Editing;
		};

	// 停止运行/暂停，回到编辑态：StopPlayback 会把播放前的快照载回来，世界指针会变
	auto StopPlaybackToEditing = [&]()->void {
		if (!IsWorldRunning())
			return;
		editorApp->Scene().StopPlayback();
		selectedEntity = Entity();
		};

	// 保存项目时顺带把当前场景也存下去
	auto SaveProjectWithScene = [&]()->void {
		if (!projectManager || !projectManager->IsProjectOpen())
			return;
		// 这里只会在编辑态被调用：运行/暂停态的入口已经先走「是否停止运行」确认
		if (editorApp && editorApp->Scene().GetEditorState() != EditorWorldState::Editing)
			return;
		SaveCurrentScene(projectManager);
		projectManager->SaveProject();
		};

	auto StopPlayConfirmPopup = [&]()->void {
		ImGui::OpenPopup("StopPlayConfirmPopup");
		if (ImGui::BeginPopupModal("StopPlayConfirmPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("当前场景正在运行（或已暂停）。");
			ImGui::Text("需要先停止运行回到编辑态，才能继续这个操作。");
			ImGui::Separator();

			if (ImGui::Button("停止并继续", ImVec2(120, 0))) {
				StopPlaybackToEditing();
				ImGui::CloseCurrentPopup();
				s_openStopPlayConfirmPopup = false;

				if (m_pendingAction == "save") {
					m_pendingAction = "";
					// 没打开项目时，场景资产不属于任何项目，没有可保存的东西，停止运行就够了
					if (projectManager->IsProjectOpen())
						SaveProjectWithScene();
				}
				else if (s_pendingNeedSaveConfirm && projectManager->IsProjectOpen()) {
					// 有项目才需要问未保存变更；pendingAction 原样留给后续确认流程
					s_openSaveConfirmPopup = true;
				}
				else {
					// 没有项目：直接执行（打开/新建/关闭都不需要问保存）
					ExecutePendingAction(window);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("取消", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				s_openStopPlayConfirmPopup = false;
				m_pendingAction = "";
			}
			ImGui::EndPopup();
		}
		};

	auto SaveConfirmPopup = [&]()->void {
		ImGui::OpenPopup("SaveConfirmPopup");
		if (ImGui::BeginPopupModal("SaveConfirmPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("当前项目有未保存的变更.");
			ImGui::Text("是否在继续操作前保存项目?");

			if (ImGui::Button("保存")) {
				SaveProjectWithScene();
				ImGui::CloseCurrentPopup();
				ExecutePendingAction(window);
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("不保存")) {
				ImGui::CloseCurrentPopup();
				ExecutePendingAction(window);
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("取消")) {
				ImGui::CloseCurrentPopup();
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::EndPopup();
		}
		};

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("文件"))
		{
			// ===== New =====
			if (ImGui::MenuItem("新建项目", "Ctrl+N")) {
				if (!RequestProjectFlow(projectManager, "new", true))
					s_openNewProjectPopup = true;
			}

			// ===== Open =====
			if (ImGui::MenuItem("打开项目", "Ctrl+O")) {
				if (!RequestProjectFlow(projectManager, "open", true)) {
					std::string filePath;
					if (FileIO::OpenOneFile(filePath, { { "project",".project" } }))
					{
						OpenProjectAndLoadDefaultScene(filePath, projectManager, editorApp);
						m_selectedFolder.clear();
					}
				}
			}

			// ===== Save（没打开项目时只用于「停止运行」，因为场景资产属于项目）=====
			if (ImGui::MenuItem("保存项目", "Ctrl+S", false, projectManager->IsProjectOpen() || IsWorldRunning())) {
				if (!RequestProjectFlow(projectManager, "save", false)) {
					if (projectManager->IsProjectOpen())
						SaveProjectWithScene();
				}
			}

			if (ImGui::MenuItem("导入资产", "Ctrl+I", false, projectManager->IsProjectOpen())) {
				if (projectManager->IsProjectOpen())
					s_assetImportPopup.SetVisiable(true);
			}

			ImGui::Separator();
			if (ImGui::MenuItem("关闭", "Alt+F4")) {
				if (!RequestProjectFlow(projectManager, "exit", true))
					glfwSetWindowShouldClose(window, true);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("编辑"))
		{
			if (ImGui::MenuItem("撤销", "Ctrl+Z")) {}
			if (ImGui::MenuItem("恢复", "Ctrl+Y")) {}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("场景"))
		{
			if (ImGui::MenuItem("保存场景", nullptr, false,
				editorApp->Scene().GetEditorState() == EditorWorldState::Editing && projectManager && projectManager->IsProjectOpen()))
				SaveCurrentScene(projectManager);
			const bool canSetDefaultScene = editorApp->Scene().GetEditorState() == EditorWorldState::Editing
				&& projectManager && projectManager->IsProjectOpen() && !m_currentSceneAssetGuid.empty();
			if (ImGui::MenuItem("设为默认场景", nullptr, false, canSetDefaultScene))
				projectManager->SetDefaultSceneGUID(m_currentSceneAssetGuid);
			ImGui::Separator();
			RenderOptionPanels::DrawMenu();		// 「渲染参数」二级菜单：各组参数的独立浮动面板
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("窗口"))
		{
			ImGui::MenuItem("场景");
			ImGui::MenuItem("场景对象");
			ImGui::MenuItem("对象属性");
			ImGui::MenuItem("项目管理");
			ImGui::Separator();
			bool thumbnails = AssetThumbnails::IsEnabled();
			if (ImGui::MenuItem("资产缩略图", nullptr, &thumbnails))
				AssetThumbnails::SetEnabled(thumbnails);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("资产浏览器里的贴图 / 精灵 / 动图显示内容缩略图（后台生成，不加载资产）。关掉后显示类型图标");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("添加"))
		{
			if (ImGui::BeginMenu("光源"))
			{
				if (ImGui::MenuItem("平行光"))
				{
					WantToAddDirLight = true;
				}
				if (ImGui::MenuItem("点光源"))
				{
					WantToAddPointLight = true;
				}
				if (ImGui::MenuItem("聚光"))
				{
					WantToAddSpotLight = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("几何体"))
			{
				if (ImGui::MenuItem("立方体"))
				{
					WantToAddCube = true;
				}
				if (ImGui::MenuItem("球体"))
				{
					WantToAddSphere = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("空实体"))
			{
				WantToAddEmptyEntity = true;
			}
			if (ImGui::MenuItem("DDGI 体积"))
			{
				WantToAddDDGIVolume = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("调试"))
		{
			ImGui::MenuItem("渲染性能", nullptr, &RenderProfilerPanel::Visible());
			ImGui::MenuItem("逻辑性能", nullptr, &LogicProfilerPanel::Visible());
			ImGui::MenuItem("DDGI 调试", nullptr, &DDGIEditor::DebugWindowVisible());

			// 阴影调试：只对场景里选中的、启用了的方向光生效，替换直接光颜色（运行时状态，不保存进场景）。
			// 没有选中方向光时画面正常显示
			if (editorApp && ImGui::BeginMenu("阴影调试"))
			{
				using ShadowDebugMode = Scene3DRenderOption::ShadowDebugMode;
				struct Item { const char* name; ShadowDebugMode mode; const char* tip; };
				static const Item items[] = {
					{ "关闭", ShadowDebugMode::Off, nullptr },
					{ "级联着色", ShadowDebugMode::Cascade, "红 0 / 绿 1 / 蓝 2 / 黄 3，阴影处变暗，渐隐段带斜纹\n品红斜纹：接收点投影出了所选级联范围\n黑白斜纹：一个级联都没分到；暗灰：超出阴影距离" },
					{ "阴影结果", ShadowDebugMode::ShadowFactor, "白 = 受光，黑 = 阴影（已含渐隐）" },
					{ "阴影图内容", ShadowDebugMode::ShadowMapContent, "接收点处阴影图存了什么：\n青 = 空（这里没有物体画进阴影图：被剔除或被裁掉）\n红 = 有更近的遮挡物；绿 = 只有接收面自己" },
				};
				auto option = editorApp->GetOption();
				bool changed = false;
				for (const Item& item : items)
				{
					if (ImGui::MenuItem(item.name, nullptr, option.shadowDebugMode == item.mode))
					{
						option.shadowDebugMode = item.mode;
						changed = true;
					}
					if (item.tip && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", item.tip);
				}
				ImGui::Separator();
				ImGui::TextDisabled("选中一个启用了的方向光实体后生效");
				ImGui::TextDisabled("暗紫 = 选中的方向光没开投影");
				if (changed)
					editorApp->SetOption(option);
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (s_openStopPlayConfirmPopup) {
		StopPlayConfirmPopup();
	}
	if (s_openSaveConfirmPopup) {
		SaveConfirmPopup();
	}
	if (s_openNewProjectPopup) {
		NewProjectPopup();
	}
	if (!s_sceneSaveErrorMessage.empty()) {
		ImGui::OpenPopup("SceneSaveErrorPopup");
		if (ImGui::BeginPopupModal("SceneSaveErrorPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(s_sceneSaveErrorMessage.c_str());
			if (ImGui::Button("确定")) {
				s_sceneSaveErrorMessage.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	DrawImageConvertConfirm();
	if (s_assetImportPopup.GetVisiable()) {
		s_assetImportPopup.DrawPopup(projectManager);
	}
}

void ImguiLayout::DrawPlayControls(EditorApp* editorApp, ProjectManager* projectManager)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 38.f));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::Begin("##PlayControls", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
	const float controlsWidth = ImGui::CalcTextSize("Paused").x + ImGui::GetStyle().ItemSpacing.x
		+ ImGui::CalcTextSize("Resume").x + ImGui::GetStyle().FramePadding.x * 2.f
		+ ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2.f;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - controlsWidth) * 0.5f);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(editorApp->Scene().GetEditorState() == EditorWorldState::Playing ? "Playing" :
		editorApp->Scene().GetEditorState() == EditorWorldState::Paused ? "Paused" : "Editing");
	ImGui::SameLine();
	if (editorApp->Scene().GetEditorState() == EditorWorldState::Editing)
	{
		if (ImGui::Button("Play"))
			editorApp->Scene().PlayWorld();
	}
	else if (editorApp->Scene().GetEditorState() == EditorWorldState::Playing)
	{
		if (ImGui::Button("Pause"))
			editorApp->Scene().PauseWorld();

		std::string sceneAsset;

		bool wantQuit = editorApp->Runtime().TakeQuitRequest();
		bool wantChangeScene = editorApp->Scene().HasPendingSceneChange();
		if (wantQuit)
		{
			editorApp->Scene().StopPlayback();
			selectedEntity = Entity();
		}
		else
		{
			if (wantChangeScene && editorApp->Scene().PumpSceneChanges())
			{
				selectedEntity = Entity();
			}
		}
	}
	else
	{
		if (ImGui::Button("Resume"))
			editorApp->Scene().ContinueWorld();
	}

	if (editorApp->Scene().GetEditorState() != EditorWorldState::Editing)
	{
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
		{
			editorApp->Scene().StopPlayback();
			selectedEntity = Entity();
		}
	}

	// 视口三层显示开关：只影响编辑器视口；关掉的层不参与点选。3D 关掉时整帧不跑 3D 渲染
	{
		auto visibility = editorApp->GetLayerVisibility();
		bool changed = false;
		ImGui::SameLine(0.0f, 24.0f);
		changed |= ImGui::Checkbox("3D", &visibility.layer3D);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("显示 3D 层（关掉时不跑 3D 渲染，也不能点选 3D 物体）");
		ImGui::SameLine();
		changed |= ImGui::Checkbox("2D", &visibility.layer2D);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("显示 2D 层（精灵、动图；关掉时也不能点选 2D 物体）");
		ImGui::SameLine();
		changed |= ImGui::Checkbox("UI", &visibility.layerUI);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("显示 UI 层（关掉时 UI 也不响应点击）");
		if (changed)
			editorApp->SetLayerVisibility(visibility);
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void ImguiLayout::SetDockspace(bool firstFrame)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// 设置窗口位置和大小：为主菜单和播放控制条预留空间
	float menuBarHeight = ImGui::GetFrameHeight();
	float statusBarHeight = ImGui::GetFrameHeightWithSpacing();
	constexpr float playControlsHeight = 38.f;

	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight + playControlsHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - playControlsHeight - statusBarHeight));

	ImGui::SetNextWindowViewport(viewport->ID);

	// 使用 PushStyleVar 让窗口背景完全透明
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	// 窗口标志
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	// 用 "##" 隐藏窗口名称，但仍然保留 ID
	ImGui::Begin("##MainDockSpaceContainer", nullptr, window_flags);
	ImGui::PopStyleVar(3);

	// 创建 Dockspace
	ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar);

	// ============ 3. 设置初始布局（仅第一帧） ============
	if (firstFrame)
	{
		// 清除已有布局
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - playControlsHeight - statusBarHeight));

		ImGuiID dock_left, dock_right, dock_bottom_left, dock_bottom_right, dock_center, dock_left_bottom;


		// 分割布局
		ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.20f, &dock_left, &dock_right);
		ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Right, 0.25f, &dock_right, &dock_center);
		ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.30f, &dock_left_bottom, &dock_left);
		ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.25f, &dock_bottom_left, &dock_center);
		ImGui::DockBuilderSplitNode(dock_bottom_left, ImGuiDir_Right, 0.75f, &dock_bottom_right, &dock_bottom_left);


		// 停靠窗口
		ImGui::DockBuilderDockWindow("Scene Objects", dock_left);
		ImGui::DockBuilderDockWindow("Options", dock_left_bottom);
		ImGui::DockBuilderDockWindow("Properties", dock_right);
		ImGui::DockBuilderDockWindow("Folder Tree", dock_bottom_left);
		ImGui::DockBuilderDockWindow("Folder", dock_bottom_right);
		ImGui::DockBuilderDockWindow("Scene View", dock_center);

		ImGui::DockBuilderFinish(dockspace_id);
	}

	ImGui::End();  // 结束 Dockspace 容器窗口
}

void ImguiLayout::DrawSceneObjectList(EditorApp* editorApp)
{
	ImGui::Begin("Scene Objects");
	ImGui::Text("场景对象");
	ImGui::Separator();

	// 获取所有实体
	auto entities = editorApp->GetWorld()->getAllEntity();

	// 构建实体列表（缓存实体 ID 和名称）
	struct EntityItem {
		Entity entity;
		std::string name;
	};
	std::vector<EntityItem> items;
	items.reserve(entities.size());

	for (auto& entity : entities)
	{
		EntityItem item;
		item.entity = entity;

		if (auto tag = entity.tryGetComponent<TagEditorName>())
		{
			item.name = tag->name;
		}
		else
		{
			item.name = std::format("Unnamed_Entity_{}", entity.getId());
		}
		items.push_back(std::move(item));
	}

	static Entity renamingEntity;
	static uint32_t renameBufferSize = 256;
	static char renameBuffer[256] = "";
	static bool isRenaming = false;
	static float renamingStartTime = 0.0f;

	// 绘制列表
	for (auto& item : items)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth;  // 让整行可点击

		ImGui::PushID(item.entity.getId());

		// 高亮选中的实体
		bool isSelected = (selectedEntity && selectedEntity == item.entity);
		bool isShowRename = false;
		if (isSelected)
		{
			flags |= ImGuiTreeNodeFlags_Selected;

			// ========== 判断是否处于重命名状态 ==========
			isShowRename = isRenaming && renamingEntity == item.entity;
			if (isShowRename)
			{
				// 重命名模式：显示输入框
				// 自动聚焦
				ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##RenameInput", renameBuffer, sizeof(renameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue))
				{
					// 按回车确认
					if (strlen(renameBuffer) > 0)
					{
						editorApp->GetWorld()->SubmitCommand([entity = item.entity, newName = std::string(renameBuffer)]() -> void {
							if (!entity) return;
							SetNameTag(entity, newName);
							});
					}
					isRenaming = false;
					renamingEntity = Entity();
				}

				// 按 ESC 取消
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					isRenaming = false;
					renamingEntity = Entity();
				}

				// 点击其他地方取消重命名
				if (!ImGui::IsItemActive() && !ImGui::IsItemHovered())
				{
					if (ImGui::GetTime() - renamingStartTime > 0.2f)
					{
						isRenaming = false;
						renamingEntity = Entity();
					}
				}
			}

		}

		if (!isShowRename)
		{
			// 使用实体 ID 作为树节点的唯一 ID
			ImGui::TreeNodeEx("##node", flags, "%s", item.name.c_str());

			// 处理点击选中
			if (ImGui::IsItemClicked())
			{
				selectedEntity = item.entity;
			}

			// 处理右键菜单（可选）
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				selectedEntity = item.entity;
				ImGui::OpenPopup("EntityContextMenu");
			}

			if (isSelected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				// 填充当前名称
				strncpy_s(renameBuffer, item.name.c_str(), sizeof(renameBuffer) - 1);
				renamingEntity = selectedEntity;
				isRenaming = true;
				renamingStartTime = ImGui::GetTime();
			}

			// 右键菜单（可选功能）
			if (ImGui::BeginPopup("EntityContextMenu"))
			{
				if (ImGui::MenuItem("Delete"))
				{
					editorApp->GetWorld()->SubmitCommand([entity = item.entity, world = editorApp->GetWorld()]() -> void {
						if (!entity || !world)
							return;
						world->destroyEntity(entity);
						}
					);
				}
				if (ImGui::MenuItem("Duplicate"))
				{
					if (Entity newEntity; editorApp->Scene().DuplicateEntity(item.entity, newEntity) && newEntity)
						selectedEntity = newEntity;
				}

				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
	}

	ImGui::End();
}

void ImguiLayout::DrawObjectProperties(ProjectManager* projectManager)
{
	ImGui::Begin("Properties");
	ImGui::Text("属性");
	ImGui::Separator();
	auto world = EditorApp::Instance()->GetWorld();
	const bool entitySelected = world && selectedEntity && world->IsValidWorldEntity(selectedEntity);
	if (entitySelected)
	{
		ImGui::Text("已选择: %d", selectedEntity.getId());
		ImGui::Separator();
		auto components = PropertiesHelper::GetAllComponents(selectedEntity);	// 获取所有组件
		PropertiesHelper::DrawAllProperties(selectedEntity, components, projectManager);		// 绘制所有属性
		ImGui::Spacing();
		if (ImGui::Button("Edit Component", ImVec2(-1.f, 36.f)))
		{
			ImGui::OpenPopup("EditComponentPopup");
			s_editComponentRemoveArmed = false;
		}
		if (ImGui::BeginPopupModal("EditComponentPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("编辑组件：左边添加，右边移除");
			ImGui::TextDisabled("按 A-Z 两栏都会跳到对应首字母；点左栏添加，点击“移除”后双击右栏移除");
			ImGui::Separator();

			// 两栏都按显示名首字母排序（大小写不敏感），同首字母的组件聚在一起。
			struct AddableComponentCodec
			{
				ComponentTypeID type;
				std::string displayName;
				bool removable = false;
			};
			const auto byDisplayName = [](const AddableComponentCodec& lhs, const AddableComponentCodec& rhs)
				{
					return std::lexicographical_compare(lhs.displayName.begin(), lhs.displayName.end(),
						rhs.displayName.begin(), rhs.displayName.end(),
						[](char left, char right)
						{
							return std::tolower(static_cast<unsigned char>(left)) < std::tolower(static_cast<unsigned char>(right));
						});
				};

			std::vector<AddableComponentCodec> addableComponents;
			std::vector<AddableComponentCodec> existingComponents;
			for (const auto& codec : NComponentCodec::GetComponentCodecs())
			{
				if (!codec.editorBehavior.addable || !codec.editorBehavior.add || !codec.has)
					continue;
				const std::string displayName = codec.displayName.empty() ? codec.type : codec.displayName;
				const bool removable = codec.editorBehavior.removable && static_cast<bool>(codec.editorBehavior.remove);
				(codec.has(selectedEntity) ? existingComponents : addableComponents).push_back({ codec.componentType, displayName, removable });
			}
			std::sort(addableComponents.begin(), addableComponents.end(), byDisplayName);
			std::sort(existingComponents.begin(), existingComponents.end(), byDisplayName);

			// 按下字母后记下两栏各自要跳到的行，绘制到那一行时把它滚进视野。
			char pressedLetter = 0;
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				for (int letter = 0; letter < 26; ++letter)
				{
					const ImGuiKey key = static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_A) + letter);
					if (ImGui::IsKeyPressed(key, false))
					{
						pressedLetter = static_cast<char>('a' + letter);
						break;
					}
				}
			}
			// 没有以该字母开头的组件时，跳到下一个字典序更靠后的组件，手感更接近“跳到那里”。
			const auto findRowForLetter = [](const std::vector<AddableComponentCodec>& components, char letter) -> int
				{
					for (int row = 0; row < static_cast<int>(components.size()); ++row)
					{
						const char firstLetter = static_cast<char>(std::tolower(
							static_cast<unsigned char>(components[row].displayName.front())));
						if (firstLetter >= letter)
							return row;
					}
					return -1;
				};
			const int jumpToAddableRow = pressedLetter ? findRowForLetter(addableComponents, pressedLetter) : -1;
			const int jumpToExistingRow = pressedLetter ? findRowForLetter(existingComponents, pressedLetter) : -1;

			const ImVec2 listSize(300.f, 340.f);
			const float columnGap = ImGui::GetStyle().ItemSpacing.x;
			const float leftColumnX = ImGui::GetCursorPosX();
			const float rightColumnX = leftColumnX + listSize.x + columnGap;

			// 两栏共用一行标题：“移除”和“已添加”同一行，两栏的列表起点也就自然对齐了。
			ImGui::TextUnformatted("未添加");
			ImGui::SameLine();
			ImGui::SetCursorPosX(rightColumnX);
			ImGui::TextUnformatted("已添加");
			ImGui::SameLine();
			// 防误操作：先点“移除”，右栏才可点；双击右栏的组件才是移除，窗口不关。
			if (s_editComponentRemoveArmed)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button("移除", ImVec2(52.f, 0.f)))
				s_editComponentRemoveArmed = !s_editComponentRemoveArmed;
			if (s_editComponentRemoveArmed)
				ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(s_editComponentRemoveArmed ? "移除模式已开启，双击右栏组件才会移除；再点一次关闭" : "点一下开启移除模式，之后双击右栏组件即可移除");

			ImGui::BeginGroup();
			ImGui::SetCursorPosX(leftColumnX);
			ImGui::BeginChild("##AddComponentAddable", listSize, ImGuiChildFlags_Borders);
			if (addableComponents.empty())
				ImGui::TextDisabled("没有可添加的组件");
			for (int row = 0; row < static_cast<int>(addableComponents.size()); ++row)
			{
				const AddableComponentCodec& component = addableComponents[row];
				ImGui::PushID(static_cast<int>(component.type));
				if (ImGui::Selectable(component.displayName.c_str()))
				{
					const Entity target = selectedEntity;
					const ComponentTypeID typeID = component.type;
					world->SubmitCommand([target, typeID]() {
						auto* targetWorld = target.getWorld();
						if (!targetWorld || !targetWorld->IsValidWorldEntity(target)) return;
						auto& codecs = NComponentCodec::GetComponentCodecs();
						auto it = std::find_if(codecs.begin(), codecs.end(), [typeID](const auto& item) {
							return item.componentType == typeID;
							});
						if (it != codecs.end() && it->editorBehavior.addable && it->editorBehavior.add
							&& it->has && !it->has(target))
							it->editorBehavior.add(target);
						});
					// 添加完不关闭窗口：下一帧这个组件会自己从左边挪到右边。
				}
				if (jumpToAddableRow == row)
					ImGui::SetScrollHereY(0.0f);
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::EndGroup();

			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::SetCursorPosX(rightColumnX);
			ImGui::BeginChild("##EditComponentExisting", listSize, ImGuiChildFlags_Borders);
			if (existingComponents.empty())
				ImGui::TextDisabled("还没有任何组件");
			// 未解锁时整栏灰掉（和以前一样）；解锁后文字转白，一眼能看出现在点下去是删除。
			if (!s_editComponentRemoveArmed)
				ImGui::BeginDisabled();
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
			for (int row = 0; row < static_cast<int>(existingComponents.size()); ++row)
			{
				const AddableComponentCodec& component = existingComponents[row];
				// 移除模式下，本身不允许移除的组件（比如 Transform）仍然灰着，避免“点了没反应”。
				const bool itemDisabled = s_editComponentRemoveArmed && !component.removable;
				ImGui::PushID(static_cast<int>(component.type));
				if (itemDisabled)
					ImGui::BeginDisabled();
				if (ImGui::Selectable(component.displayName.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)
					&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
					&& s_editComponentRemoveArmed && component.removable)
				{
					const Entity target = selectedEntity;
					const ComponentTypeID typeID = component.type;
					world->SubmitCommand([target, typeID]() {
						auto* targetWorld = target.getWorld();
						if (!targetWorld || !targetWorld->IsValidWorldEntity(target)) return;
						auto& codecs = NComponentCodec::GetComponentCodecs();
						auto it = std::find_if(codecs.begin(), codecs.end(), [typeID](const auto& item) {
							return item.componentType == typeID;
							});
						if (it != codecs.end() && it->editorBehavior.removable && it->editorBehavior.remove
							&& it->has && it->has(target))
							it->editorBehavior.remove(target);
						});
				}
				if (jumpToExistingRow == row)
					ImGui::SetScrollHereY(0.0f);
				if (itemDisabled)
					ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (!s_editComponentRemoveArmed)
				ImGui::EndDisabled();
			else
				ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::EndGroup();

			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(120.f, 0.f)))
			{
				// 关窗口时复位移除模式，下次打开默认是安全状态。
				s_editComponentRemoveArmed = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else
		{
			// Esc 等其它关闭方式也复位。
			s_editComponentRemoveArmed = false;
		}
	}
	else
	{
		ImGui::Text("未选中实体");
		ImGui::Separator();
	}

	ImGui::End();
}

void ImguiLayout::DrawMaterialEditor(ProjectManager* projectManager)
{
	if (!s_materialEditorOpen || !projectManager)
		return;

	auto database = projectManager->GetDatabase();
	auto meta = database ? database->GetAssetMetaByGuid(s_materialEditorGuid) : nullptr;
	if (!meta || meta->type != AssetType::Material)
	{
		s_materialEditorOpen = false;
		return;
	}

	auto assetObject = projectManager->LoadAsset(meta->guid);
	auto materialAsset = assetObject ? std::dynamic_pointer_cast<MaterialAsset>(assetObject) : nullptr;
	if (!materialAsset)
	{
		s_materialEditorOpen = false;
		s_materialOrigin.valid = false;
		return;
	}
	if (!s_materialOrigin.valid || s_materialOrigin.guid != meta->guid)
	{
		s_materialOrigin.guid = meta->guid;
		s_materialOrigin.properties = materialAsset->GetProperties();
		s_materialOrigin.textures = materialAsset->GetTextureDependencies();
		s_materialOrigin.valid = true;
	}
	auto restoreOrigin = [&]() {
		if (!s_materialOrigin.valid || s_materialOrigin.guid != meta->guid)
			return;
		materialAsset->SetProperties(s_materialOrigin.properties);
		std::vector<TextureType> currentTextures;
		for (const auto& [type, textureGuid] : materialAsset->GetTextureDependencies())
			currentTextures.push_back(type);
		for (TextureType type : currentTextures)
			materialAsset->SetTextureDependency(type, {}, nullptr);
		for (const auto& [type, textureGuid] : s_materialOrigin.textures)
		{
			auto textureObject = projectManager->LoadAsset(textureGuid);
			auto texture = textureObject ? textureObject->AsTexture() : nullptr;
			if (texture)
				materialAsset->SetTextureDependency(type, textureGuid, texture);
		}
		};

	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(std::format("Material Editor: {}###MaterialEditor", meta->name).c_str(), &open, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
		s_materialEditorOpen = open;
		if (!open)
		{
			restoreOrigin();
			s_materialOrigin.valid = false;
		}
		return;
	}

	bool changed = false;
	auto properties = materialAsset->GetProperties();
	const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
	ImGui::BeginChild("MaterialEditorContent", ImVec2(0.f, -footerHeight), ImGuiChildFlags_None);

	// ---- 属性：两列网格（左标签、右控件占满），比"控件 + 右侧标签、固定宽度"紧凑 ----
	auto drawProperties = [&]()
		{
			ImGui::SeparatorText("Material Properties");
			const float labelWidth = ImGui::CalcTextSize("Emission Strength").x + ImGui::GetStyle().ItemSpacing.x;
			if (!ImGui::BeginTable("##MaterialProps", 2, ImGuiTableFlags_SizingFixedFit))
				return;
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			auto row = [](const char* label)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);
				};
			row("Albedo");
			changed |= ImGui::ColorEdit3("##Albedo", glm::value_ptr(properties.albedo));
			row("Metallic");
			changed |= ImGui::DragFloat("##Metallic", &properties.metallic, 0.01f, 0.f, 1.f);
			row("Roughness");
			changed |= ImGui::DragFloat("##Roughness", &properties.roughness, 0.01f, 0.001f, 1.f);
			row("Ambient Occlusion");
			changed |= ImGui::DragFloat("##AO", &properties.ambientOcclusion, 0.01f, 0.f, 1.f);
			row("Opacity");
			changed |= ImGui::DragFloat("##Opacity", &properties.opacity, 0.01f, 0.f, 1.f);
			row("IOR");
			changed |= ImGui::DragFloat("##IOR", &properties.IOR, 0.01f, 1.f, 3.f);
			row("Emission Color");
			changed |= ImGui::ColorEdit3("##EmissionColor", glm::value_ptr(properties.emissionColor));
			row("Emission Strength");
			changed |= ImGui::DragFloat("##EmissionStrength", &properties.emissionStrength, 0.01f, 0.f, 100.f);

			int alphaMode = static_cast<int>(properties.alphamode);
			const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
			row("Alpha Mode");
			if (ImGui::Combo("##AlphaMode", &alphaMode, alphaModes, IM_ARRAYSIZE(alphaModes)))
			{
				properties.alphamode = static_cast<AlphaMode>(alphaMode);
				changed = true;
			}
			if (properties.alphamode == AlphaMode::Mask)
			{
				row("Mask Threshold");
				changed |= ImGui::DragFloat("##MaskThreshold", &properties.maskthreshold, 0.01f, 0.f, 1.f);
			}
			row("Two Sided");
			changed |= ImGui::Checkbox("##TwoSided", &properties.twosided);
			ImGui::EndTable();
		};

	// ---- 贴图槽：一行一个（40px 缩略图 | 槽名 + 贴图名 | 清除），整行都能拖入 ----
	auto acceptTextureDrop = [&](TextureType type)
		{
			if (!ImGui::BeginDragDropTarget())
				return;
			if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const auto* guidData = static_cast<const char*>(payload->Data);
				if (guidData && guidData[0] != '\0')
				{
					auto textureMeta = database->GetAssetMetaByGuid(AssetGUID(guidData));
					if (textureMeta && textureMeta->type == AssetType::Texture)
					{
						auto textureObject = projectManager->LoadAsset(textureMeta->guid);
						auto texture = textureObject ? textureObject->AsTexture() : nullptr;
						if (texture)
							materialAsset->SetTextureDependency(type, textureMeta->guid, texture);
					}
					else if (textureMeta && textureMeta->type == AssetType::Sprite)
					{
						// 精灵拖进贴图栏：先用精灵的原图生成一张贴图，导入成功后填进这个栏
						const AssetGUID materialGuid = meta->guid;
						const TextureType slotType = type;
						RequestTextureFromSprite(textureMeta->guid, projectManager, [projectManager, materialGuid, slotType](const AssetMeta& created)
							{
								auto material = std::dynamic_pointer_cast<MaterialAsset>(projectManager->LoadAsset(materialGuid));
								auto textureObject = projectManager->LoadAsset(created.guid);
								auto texture = textureObject ? textureObject->AsTexture() : nullptr;
								if (material && texture)
									material->SetTextureDependency(slotType, created.guid, texture);
							});
					}
				}
			}
			ImGui::EndDragDropTarget();
		};

	auto drawTextures = [&]()
		{
			ImGui::SeparatorText("Texture Slots");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("把贴图（或精灵，会先生成贴图）资产拖到槽上");

			const std::pair<TextureType, const char*> textureSlots[] = {
				{ TextureType::Albedo, "Albedo" },
				{ TextureType::Metallic, "Metallic" },
				{ TextureType::Roughness, "Roughness" },
				{ TextureType::Normal, "Normal" },
				{ TextureType::AO, "AO" },
				{ TextureType::Emissive, "Emissive" },
				{ TextureType::MetallicRoughness, "Metallic Roughness" },
				{ TextureType::Height, "Height" },
				{ TextureType::Opacity, "Opacity" }
			};
			constexpr float slotSide = 40.0f;
			if (!ImGui::BeginTable("##TextureSlots", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
				return;
			ImGui::TableSetupColumn("thumb", ImGuiTableColumnFlags_WidthFixed, slotSide);
			ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("clear", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

			const auto& dependencies = materialAsset->GetTextureDependencies();
			std::optional<TextureType> clearType;
			for (const auto& [type, label] : textureSlots)
			{
				ImGui::PushID(static_cast<int>(type));
				auto dependency = dependencies.find(type);
				const bool assigned = dependency != dependencies.end();
				const AssetGUID guid = assigned ? dependency->second : AssetGUID{};
				auto textureMeta = assigned ? database->GetAssetMetaByGuid(guid) : nullptr;
				const std::string textureName = !assigned ? std::string("None") : (textureMeta ? textureMeta->name : guid);

				ImGui::TableNextRow(ImGuiTableRowFlags_None, slotSide);

				ImGui::TableSetColumnIndex(0);
				AssetThumbnails::DrawThumbnailBox(projectManager, guid, slotSide);
				acceptTextureDrop(type);

				// 槽名（暗色）+ 贴图名两行；整个格子是一个不可见按钮，作为拖放目标
				ImGui::TableSetColumnIndex(1);
				const ImVec2 cellPos = ImGui::GetCursorScreenPos();
				const float cellWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
				ImGui::InvisibleButton("##slot", ImVec2(cellWidth, slotSide));
				const bool hovered = ImGui::IsItemHovered();
				acceptTextureDrop(type);
				{
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const float lineHeight = ImGui::GetTextLineHeight();
					const float top = cellPos.y + (slotSide - lineHeight * 2.0f - 2.0f) * 0.5f;
					drawList->PushClipRect(cellPos, ImVec2(cellPos.x + cellWidth, cellPos.y + slotSide), true);
					drawList->AddText(ImVec2(cellPos.x, top), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
					drawList->AddText(ImVec2(cellPos.x, top + lineHeight + 2.0f),
						ImGui::GetColorU32(assigned ? ImGuiCol_Text : ImGuiCol_TextDisabled), textureName.c_str());
					drawList->PopClipRect();
				}
				if (hovered)
				{
					if (assigned && textureMeta)
						ImGui::SetTooltip("%s\n%s", textureMeta->name.c_str(), textureMeta->path.c_str());
					else if (assigned)
						ImGui::SetTooltip("资产不存在：%s", guid.c_str());
					else
						ImGui::SetTooltip("把贴图资产拖到这里");
				}

				ImGui::TableSetColumnIndex(2);
				if (assigned)
				{
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (slotSide - ImGui::GetFrameHeight()) * 0.5f);
					if (ImGui::Button("×", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
						clearType = type;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("清除");
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
			// 遍历时引用着 dependencies，清除放到循环外面
			if (clearType)
				materialAsset->SetTextureDependency(*clearType, {}, nullptr);
		};

	// 窗口够宽就左右两栏，窄了就上下排
	const bool twoColumns = ImGui::GetContentRegionAvail().x >= 600.0f;
	if (ImGui::BeginTable("MaterialEditorColumns", twoColumns ? 2 : 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		if (twoColumns)
			ImGui::TableSetupColumn("Textures", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		drawProperties();
		if (twoColumns)
			ImGui::TableSetColumnIndex(1);
		else
			ImGui::Spacing();
		drawTextures();
		ImGui::EndTable();
	}
	ImGui::EndChild();
	if (changed)
		materialAsset->SetProperties(properties);
	ImGui::Separator();
	ImGui::BeginChild("MaterialEditorFooter", ImVec2(0.f, footerHeight), ImGuiChildFlags_None);
	if (open)
	{
		if (ImGui::Button("Save"))
		{
			if (!projectManager->SaveAsset(meta->guid))
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "材质保存失败");
			}
			else
			{
				s_materialOrigin.properties = materialAsset->GetProperties();
				s_materialOrigin.textures = materialAsset->GetTextureDependencies();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Restore"))
			restoreOrigin();
	}
	ImGui::EndChild();
	ImGui::End();
	s_materialEditorOpen = open;
	if (!open)
	{
		restoreOrigin();
		s_materialOrigin.valid = false;
	}
}

void ImguiLayout::DrawAssetBrowser(ProjectManager* projectManager)
{
	{
		ImGui::Begin("Folder Tree");
		ImGui::Text("文件树");
		ImGui::Separator();

		if (!projectManager->IsProjectOpen()) {
			ImGui::Text("未打开项目");
		}
		else
		{
			// 递归显示文件夹
			auto projectPath = projectManager->GetProjectFolderFullPath();
			DrawFolderTree(projectPath, projectPath, projectPath);

		}
		ImGui::End();
	}

	{

		ImGui::Begin("Folder");

		if (!projectManager->IsProjectOpen()) {
			ImGui::Text("未打开文件夹");
			ImGui::Separator();
		}
		else
		{
			fs::path selectedFullFolderPath = fs::path(projectManager->GetProjectAssetFullPath(m_selectedFolder));
			if (fs::exists(selectedFullFolderPath))
			{
				DrawFolderContent(projectManager, selectedFullFolderPath.string());
			}
			else
				ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No Folders open");
		}

		ImGui::End();
	}
	DrawMaterialEditor(projectManager);
	Sprite2DEditor::DrawWindows(projectManager);
	TextAssetEditor::DrawWindows(projectManager);
}

void ImguiLayout::DrawSceneView(EditorApp* editorApp, ProjectManager* projectManager)
{
	ImGui::Begin("Scene View");

	ImGuizmo::BeginFrame();
	ImGuizmo::SetDrawlist();

	// 获取场景视口区域
	ImVec2 viewportPos = ImGui::GetCursorScreenPos();
	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	// 引擎 UI（ImGui 拾取）要知道渲染目标摆在窗口的哪块矩形里。
	if (auto inputContext = editorApp->GetInputContext())
		inputContext->SetViewportRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
	ImGuizmo::SetRect(viewportPos.x, viewportPos.y,
		viewportSize.x, viewportSize.y);

	// DDGI：推送调试设置（可视化、选中体积 / 探针、漏光像素）、处理烘焙结果
	DDGIEditor::UpdateFrame(editorApp, selectedEntity);

	// 阴影调试：只对选中的、启用了的方向光生效。选中别的实体 / 灯被禁用时渲染器收到 UINT32_MAX，按正常画面显示
	{
		uint32_t shadowDebugEntity = UINT32_MAX;
		if (selectedEntity && selectedEntity.isValid())
		{
			if (auto* light = selectedEntity.tryGetComponent<RenderLight>())
			{
				if (light->visible && light->type == LightType::Directional)
					shadowDebugEntity = selectedEntity.getId();
			}
		}
		auto option = editorApp->GetOption();
		if (option.shadowDebugEntity != shadowDebugEntity)
		{
			option.shadowDebugEntity = shadowDebugEntity;
			editorApp->SetOption(option);
		}
	}

	{
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool inside = viewportSize.x > 0 && viewportSize.y > 0
			&& mouse.x >= viewportPos.x && mouse.y >= viewportPos.y
			&& mouse.x < viewportPos.x + viewportSize.x && mouse.y < viewportPos.y + viewportSize.y
			&& ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		DDGIEditor::SetViewportHover(inside, glm::vec2((mouse.x - viewportPos.x) / std::max(1.0f, viewportSize.x),
			(mouse.y - viewportPos.y) / std::max(1.0f, viewportSize.y)));
	}

	if (WantToAddDirLight || WantToAddPointLight || WantToAddSpotLight || WantToAddCube || WantToAddSphere || WantToAddEmptyEntity || WantToAddDDGIVolume)
	{
		glm::mat4 addView(1.f), addProjection(1.f);
		glm::vec3 rayOrigin(0.f), rayDirection(0.f, 0.f, -1.f);
		if (!editorApp->GetCameraSnapshot(addView, addProjection, rayOrigin, rayDirection))
			return;

		auto result = editorApp->Scene().RayCast(rayOrigin, rayDirection);
		glm::vec3 position = result.hit ? result.hitPoint : rayOrigin + rayDirection * 15.f;

		if (WantToAddDirLight)
		{
			editorApp->Scene().AddLight(LightType::Directional, position);
			WantToAddDirLight = false;
		}
		if (WantToAddPointLight)
		{
			editorApp->Scene().AddLight(LightType::Point, position);
			WantToAddPointLight = false;
		}
		if (WantToAddSpotLight)
		{
			editorApp->Scene().AddLight(LightType::Spot, position);
			WantToAddSpotLight = false;
		}
		if (WantToAddCube)
		{
			editorApp->Scene().AddGeometry(GeometryType::Cube, position);
			WantToAddCube = false;
		}
		if (WantToAddSphere)
		{
			editorApp->Scene().AddGeometry(GeometryType::Sphere, position);
			WantToAddSphere = false;
		}
		if (WantToAddEmptyEntity)
		{
			editorApp->Scene().AddEmptyEntity(position);
			WantToAddEmptyEntity = false;
		}
		if (WantToAddDDGIVolume)
		{
			editorApp->Scene().AddDDGIVolume(position);
			WantToAddDDGIVolume = false;
		}
	}

	// 显示场景渲染
	if (editorApp->IsRenderDeviceReady() && viewportSize.x > 0 && viewportSize.y > 0)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

		static Task<bool> targetInitializationTask;
		static bool targetInitialized = false;
		static bool targetInitializationFailed = false;

		if (!targetInitialized)
		{
			if (!targetInitializationTask)
			{
				auto width = static_cast<uint32_t>(viewportSize.x);
				auto height = static_cast<uint32_t>(viewportSize.y);
				targetInitializationTask = CoroTask::Run([width, height, editorApp]() mutable -> bool {
					try
					{
						if (editorApp->CreateRenderTarget(width, height))
						{
							editorApp->StartSceneRender();
							return true;
						}
						return false;
					}
					catch (const std::exception&)
					{
						return false;
					}
					});
			}
			else if (targetInitializationTask && targetInitializationTask.is_done())
			{
				auto width = static_cast<uint32_t>(viewportSize.x);
				auto height = static_cast<uint32_t>(viewportSize.y);
				if (editorApp->GetRenderTargetWidth() != width || editorApp->GetRenderTargetHeight() != height)
				{
					targetInitializationTask = CoroTask::Run([width, height, editorApp]() mutable -> bool {
						try
						{
							return editorApp->ResizeRenderTarget(width, height);
						}
						catch (const std::exception&)
						{
							return false;
						}
						});
				}
				else
				{
					try
					{
						targetInitializationFailed = !targetInitializationTask.sync_wait();
					}
					catch (const std::exception&)
					{
						targetInitializationFailed = true;
					}
					targetInitialized = true;
				}
			}
		}

		if (targetInitialized && !targetInitializationFailed && editorApp->IsRenderTargetReady())
		{
			static std::unordered_map<VkImageView, VkDescriptorSet> imguiRegisterTexture;

			auto prevClear = [&]()
				{
					{
						LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());
						vkDeviceWaitIdle(g_Device);
						for (auto& [view, textureId] : imguiRegisterTexture)
						{
							if (textureId != VK_NULL_HANDLE)
								ImGui_ImplVulkan_RemoveTexture(textureId);
						}
					}
					imguiRegisterTexture.clear();
				};

			editorApp->ResizeRenderer(viewportSize.x, viewportSize.y, prevClear);

			editorApp->WaitImage([&](std::shared_ptr<Texture2D> tex) {
				if (!tex) return;

				VkImageView view = tex->GetImageView(vk::ImageAspectFlagBits::eColor);
				if (view == VK_NULL_HANDLE)
					return;

				VkDescriptorSet textureId;
				auto it = imguiRegisterTexture.find(view);
				if (it == imguiRegisterTexture.end())
				{
					LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());
					textureId =
						ImGui_ImplVulkan_AddTexture(
							view,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
						);

					imguiRegisterTexture[view] = textureId;
				}
				else
				{
					textureId = it->second;
				}

				ImGui::Image(
					(ImTextureID)textureId,
					viewportSize,
					ImVec2(0, 0),
					ImVec2(1, 1)
				);

				});
		}
		else
		{
			// 当前视口左上角和可用区域
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const ImVec2 size = viewportSize;

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			// 视口背景
			drawList->AddRectFilled(
				pos,
				ImVec2(pos.x + size.x, pos.y + size.y),
				IM_COL32(30, 30, 30, 255)
			);

			const ImVec2 center(
				pos.x + size.x * 0.5f,
				pos.y + size.y * 0.5f - 12.0f
			);

			if (targetInitializationFailed)
			{
				const char* message = "Scene Renderer Initialization Failed";
				const ImVec2 textSize = ImGui::CalcTextSize(message);

				drawList->AddText(
					ImVec2(center.x - textSize.x * 0.5f,
						center.y + 30.0f),
					IM_COL32(255, 100, 100, 255),
					message
				);
			}
			else
			{
				// 旋转加载圆环
				const float time = static_cast<float>(ImGui::GetTime());
				const float startAngle = time * 4.0f;
				const float radius = 16.0f;

				drawList->PathClear();
				drawList->PathArcTo(
					center,
					radius,
					startAngle,
					startAngle + 4.2f,
					32
				);
				drawList->PathStroke(
					IM_COL32(80, 170, 255, 255),
					ImDrawFlags_None,
					3.0f
				);

				// 加载文字
				const char* message = "Initializing Scene Renderer...";
				const ImVec2 textSize = ImGui::CalcTextSize(message);

				drawList->AddText(
					ImVec2(center.x - textSize.x * 0.5f,
						center.y + 30.0f),
					IM_COL32(220, 220, 220, 255),
					message
				);

				// 动态省略号：Initializing Vulkan.
				//                         ..
				//                         ...
				const int dots =
					static_cast<int>(time * 2.0f) % 3 + 1;

				char loadingText[64] = "Loading";
				for (int i = 0; i < dots; ++i)
				{
					const size_t len = strlen(loadingText);
					loadingText[len] = '.';
					loadingText[len + 1] = '\0';
				}

				const ImVec2 loadingSize =
					ImGui::CalcTextSize(loadingText);

				drawList->AddText(
					ImVec2(center.x - loadingSize.x * 0.5f,
						center.y + 52.0f),
					IM_COL32(160, 160, 160, 255),
					loadingText
				);
			}

			// 为视口占据布局空间，避免后续 ImGui 控件重叠
			ImGui::Dummy(size);
		}


		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0) {
					AssetGUID guid(data);
					if (auto meta = projectManager->GetDatabase()->GetAssetMetaByGuid(guid))
						HandleAssetDrop(*meta, editorApp, projectManager, viewportPos, viewportSize);
				}
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0)
				{
					std::string fileFullPath(data);
					std::string ext = fs::path(fileFullPath).extension().string();
					if (ext == ".lua" || ext == ".ui")
					{
						glm::mat4 view(1.f), projection(1.f);
						glm::vec3 cameraPosition(0.f), cameraDirection(0.f, 0.f, -1.f);
						if (!editorApp->GetCameraSnapshot(view, projection, cameraPosition, cameraDirection))
							return;

						glm::mat4 projOG = projection;
						projOG[1][1] *= -1;

						glm::vec3 rayOrigin, rayDirection;
						ImGuizmo::ComputeMouseRay(
							(float*)&view,
							(float*)&projOG,
							ImGui::GetMousePos(),
							viewportPos,
							viewportSize,
							(float*)&rayOrigin,
							(float*)&rayDirection
						);

						auto entity = editorApp->Scene().PickObject(rayOrigin, rayDirection);
						if (entity && editorApp->GetWorld()->IsValidWorldEntity(entity))
						{
							if (ext == ".ui")
								HandleUIAssetFileDrop(fileFullPath, entity, projectManager);
							else
								HandleScriptFileDrop(fileFullPath, entity, projectManager);
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 相机矩阵取渲染线程最近一帧的快照（不再从生产者的三缓冲里抢）。
		glm::mat4 view(1.f), projection(1.f);
		glm::vec3 cameraPosition(0.f), cameraDirection(0.f, 0.f, -1.f);
		const bool hasCamera = editorApp->GetCameraSnapshot(view, projection, cameraPosition, cameraDirection);

		const bool use2DGizmo = Use2DGizmo(selectedEntity);
		if (selectedEntity && (hasCamera || use2DGizmo))
		{
			auto* transform = selectedEntity.tryGetComponent<Transform>();
			auto* physics = selectedEntity.tryGetComponent<Physics>();
			if (use2DGizmo)
			{
				Draw2DGizmo(editorApp, selectedEntity, viewportSize, mCurrentGizmoOperation, mCurrentGizmoMode, useSnap ? snap : nullptr);
			}
			else if (transform && hasCamera)
			{
				glm::mat4 projOG = projection;
				projOG[1][1] *= -1;

				auto transformMatrix = transform->GetMatrix();
				glm::mat4 deltaMatrix = glm::mat4(1.0f);

				// 显示 Gizmo 并操作
				ImGuizmo::SetOrthographic(false);
				ImGuizmo::Manipulate(
					(float*)glm::value_ptr(view),
					(float*)glm::value_ptr(projOG),
					mCurrentGizmoOperation,
					mCurrentGizmoMode,
					(float*)glm::value_ptr(transformMatrix),
					(float*)glm::value_ptr(deltaMatrix),
					useSnap ? snap : nullptr
				);

				// DDGI 体积：松开 Gizmo 时把缩放折算成探针数、中心吸附到网格
				static bool s_gizmoWasUsing = false;
				const bool gizmoUsing = ImGuizmo::IsUsing();
				if (s_gizmoWasUsing && !gizmoUsing && DDGIEditor::IsVolume(selectedEntity))
					DDGIEditor::FinalizeVolumeTransform(selectedEntity);
				s_gizmoWasUsing = gizmoUsing;

				if (ImGuizmo::IsUsing())
				{
					editorApp->GetWorld()->SubmitCommand(
						[newMatrix = transformMatrix, entity = selectedEntity]() mutable ->void {
							auto* transform = entity.tryGetComponent<Transform>();
							auto* physics = entity.tryGetComponent<Physics>();
							auto* runtime = entity.tryGetComponent<PhysicsRuntime>();
							if (!transform)
								return;

							transform->SetMatrix(newMatrix);

							if (physics && runtime)
							{
								physics->forceSyncTransform = true;
								if (runtime->body && !runtime->body->isActive())
									runtime->body->activate(true);
								if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
									runtime->ghostObject->activate(true);
							}

						}
					);
				}
			}

			ImGuiIO& io = ImGui::GetIO();
			// 有弹窗（比如 Add Component）时，字母键归弹窗用（跳转首字母），不能再顺手切 Gizmo。
			const bool anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
			if (io.WantCaptureKeyboard && !anyPopupOpen)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_T))
					mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
				if (ImGui::IsKeyPressed(ImGuiKey_E))
					mCurrentGizmoOperation = ImGuizmo::ROTATE;
				if (ImGui::IsKeyPressed(ImGuiKey_R))
					mCurrentGizmoOperation = ImGuizmo::SCALE;
				if (ImGui::IsKeyPressed(ImGuiKey_L))
					mCurrentGizmoMode = (mCurrentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
				if (ImGui::IsKeyPressed(ImGuiKey_Q))
					useSnap = !useSnap;
			}

			if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			{
				// 右键菜单
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("EntityContextMenu");
			}
		}

		if (hasCamera)
		{
			glm::mat4 projGL = projection;
			projGL[1][1] *= -1;
			DDGIEditor::DrawViewportOverlay(editorApp, selectedEntity, view, projGL,
				glm::vec2(viewportPos.x, viewportPos.y), glm::vec2(viewportSize.x, viewportSize.y));
		}

		if (ImGui::BeginPopup("EntityContextMenu"))
		{
			if (ImGui::MenuItem("Delete"))
			{
				editorApp->GetWorld()->SubmitCommand([entity = selectedEntity, world = editorApp->GetWorld()]() -> void {
					if (!entity || !world)
						return;
					world->destroyEntity(entity);
					}
				);
				selectedEntity = Entity();
			}
			if (ImGui::MenuItem("Duplicate"))
			{
				if (Entity newEntity; editorApp->Scene().DuplicateEntity(selectedEntity, newEntity) && newEntity)
					selectedEntity = newEntity;
			}

			ImGui::EndPopup();
		}

		editorApp->Scene().SetInputActive(ImGui::IsItemHovered());
		if (ImGui::IsItemHovered())
		{
			ImGuiIO& io = ImGui::GetIO();
			ImVec2 mousePos = ImGui::GetMousePos();

			if (!ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
			{

				// 1. 鼠标左键点击 - 拾取物体
				if (ImGui::IsMouseClicked(0))
				{
					// 获取摄像机的 View 和 Projection 矩阵
					glm::mat4 view(1.f), projection(1.f);
					glm::vec3 snapPos(0.f), snapDir(0.f, 0.f, -1.f);
					editorApp->GetCameraSnapshot(view, projection, snapPos, snapDir);

					glm::mat4 projOG = projection;
					projOG[1][1] *= -1;

					// 计算射线
					glm::vec3 rayOrigin;
					glm::vec3 rayDirection;
					ImGuizmo::ComputeMouseRay(
						(float*)&view,
						(float*)&projOG,
						mousePos,
						viewportPos,
						viewportSize,
						(float*)&rayOrigin,
						(float*)&rayDirection
					);

					// 拾取（DDGI 探针编辑模式下先点选探针）。和绘制顺序一致：2D 层在上面先选，2D 没点中才选 3D；关掉的层不参与
					if (!DDGIEditor::HandleViewportClick(selectedEntity, rayOrigin, rayDirection, io.KeyShift))
					{
						const auto visibility = editorApp->GetLayerVisibility();
						Entity picked;
						if (visibility.layer2D)
							picked = Pick2DAtMouse(editorApp, mousePos, viewportPos, viewportSize);
						s_pickedBy2D = picked;
						if (!picked && visibility.layer3D)
							picked = editorApp->Scene().PickObject(rayOrigin, rayDirection);
						selectedEntity = picked;
					}

					//if (selectedEntity)
					//	editorApp->SelectObject(selectedEntity);
					//else
					//	editorApp->DeselectObject();
				}

				// 3. 鼠标右键拖拽 - 旋转摄像机
				if (ImGui::IsMouseDragging(1))
				{
					ImVec2 delta = ImGui::GetMouseDragDelta(1);
					editorApp->Scene().RotateCamera(delta.x, delta.y);
					ImGui::ResetMouseDragDelta(1);
				}

				// 4. 鼠标中键拖拽 - 平移摄像机
				if (ImGui::IsMouseDragging(2))
				{
					ImVec2 delta = ImGui::GetMouseDragDelta(2);
					editorApp->Scene().PanCamera(delta.x, delta.y);
					ImGui::ResetMouseDragDelta(2);
				}

				// 5. 滚轮缩放
				if (io.MouseWheel != 0)
				{
				}
			}
		}

		std::string sceneLabel;
		if (m_currentSceneAssetGuid.empty())
		{
			sceneLabel = "当前场景：临时场景（未保存）";
		}
		else if (projectManager && projectManager->IsProjectOpen())
		{
			if (auto database = projectManager->GetDatabase())
			{
				if (auto meta = database->GetAssetMetaByGuid(m_currentSceneAssetGuid))
					sceneLabel = "当前场景：" + meta->path;
			}
		}
		if (sceneLabel.empty())
			sceneLabel = "当前场景资产未找到：" + m_currentSceneAssetGuid;

		// ========== 显示信息 ==========
		// 左上角显示场景信息和操作模式
		std::string text = sceneLabel
			+ std::format("\nGizmo: {} | {} {}", GetOpName(mCurrentGizmoOperation), GetModeName(mCurrentGizmoMode), useSnap ? "[Snap]" : "")
			+ "\nShortcuts:"
			+ "\nQ - Toggle Snap"
			+ "\nL - Toggle World/Local"
			+ "\nT - Translate"
			+ "\nE - Rotate"
			+ "\nR - Scale";

		ImGui::SetCursorPos(ImVec2(15, 15));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopStyleColor();

		ImGui::PopItemFlag();
	}

	ImGui::End();
}

void ImguiLayout::DrawOptions(EditorApp* editorApp)
{
	ImGui::Begin("Options");
	ImGui::Text("渲染选项");
	ImGui::Separator();

	PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.55f);
	int targetFps = static_cast<int>(editorApp->GetTargetFps());
	if (ImGui::SliderInt("目标帧率", &targetFps, 0, 360, targetFps == 0 ? "不限" : "%d FPS"))
		editorApp->SetTargetFps(static_cast<uint32_t>(targetFps < 0 ? 0 : targetFps));

	PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.55f);
	float timeScale = editorApp->Runtime().GetWorldTimeScale();
	if (ImGui::SliderFloat("世界时间膨胀", &timeScale, 0.0f, 20.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
		editorApp->Runtime().SetWorldTimeScale(timeScale);
	ImGui::SameLine();
	if (ImGui::SmallButton("重置 1x"))
		editorApp->Runtime().SetWorldTimeScale(1.0f);

	ImGui::Separator();

	auto option = editorApp->GetOption();

	// 这里只保留功能开关与后处理参数；其余各组参数在「场景 → 渲染参数」里，
	// 或点开关右边的「参数」按钮打开（同一个独立浮动面板）
	bool change = false;
	if (ImGui::CollapsingHeader("PostProcessFlags", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushID("PostProcessFlags");
		change |= RenderOptionPanels::DrawFlags(option);
		ImGui::PopID();
	}
	change |= PropertiesHelper::DrawWithTitle("PostProcessParams", option.postProcessParams);
	if (change)
		editorApp->SetOption(option);

	ImGui::End();

	// 独立浮动窗口（不参与停靠）
	RenderOptionPanels::Draw(editorApp);	// 「场景 → 渲染参数」/ 开关右边的「参数」
	DDGIEditor::DrawDebugWindow(editorApp);	// 「调试 → DDGI 调试」
	DDGIEditor::DrawLeakWindow(editorApp);
	RenderProfilerPanel::Draw(editorApp);	// 「调试 → 渲染性能」；关着时只负责关闭统计
	LogicProfilerPanel::Draw(editorApp);	// 「调试 → 逻辑性能」；关着时只负责关闭统计
}

void ImguiLayout::DrawStatusBar(EditorApp* editorApp)
{
	// 获取视口信息
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	const float statusBarHeight = ImGui::GetFrameHeightWithSpacing();

	// 设置状态栏位置：屏幕底部
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statusBarHeight));

	// 关键：确保状态栏在 Z 序最上层
	ImGui::SetNextWindowViewport(viewport->ID);

	// 窗口样式
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));  // 深灰色背景
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

	ImGui::Begin("##StatusBar", nullptr, flags);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);

	// 用 ImDrawList 在顶部画一条亮色分割线，让状态栏更明显
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	drawList->AddLine(
		ImVec2(windowPos.x, windowPos.y),
		ImVec2(windowPos.x + windowSize.x, windowPos.y),
		IM_COL32(80, 80, 80, 255),
		1.5f
	);

	if (ImGui::BeginTable("StatusBarTable", 3, ImGuiTableFlags_None))
	{
		ImGui::TableSetupColumn("##col0", ImGuiTableColumnFlags_WidthFixed, 200.0f);
		ImGui::TableSetupColumn("##col1", ImGuiTableColumnFlags_WidthFixed, 180.0f);
		ImGui::TableSetupColumn("##col2", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		// ---- 第1列：场景信息 ----
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "实体数量: %d", editorApp->GetWorld()->getAllEntityCount());

		// ---- 第2列：选中的物体 ----
		ImGui::TableNextColumn();
		if (selectedEntity) {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "选择实体: %d", selectedEntity.getId());
		}
		else {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "未选中实体");
		}

		// ---- 第3列：FPS（右对齐） ----
		ImGui::TableNextColumn();
		float fps = ImGui::GetIO().Framerate;
		float ms = 1000.0f / fps;
		float textWidth = ImGui::CalcTextSize("FPS: 000.0  (00.00ms)").x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - textWidth - 8.0f);
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FPS: %.1f  (%.2fms)", fps, ms);

		ImGui::EndTable();
	}
	ImGui::End();
}

void ImguiLayout::DropFile(const std::string& filePathStr, ProjectManager* projectManager, const ImVec2& mousePos)
{
	fs::path filePath(filePathStr);
	if (filePath.string().empty())
		return;

	// 拖入项目文件
	if (filePath.extension().string() == ".project")
	{
		memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
		strcpy(m_wantOpenProjectPathBuffer, filePath.string().c_str());

		// 运行中先问是否停止运行；没有项目则直接打开
		if (!RequestProjectFlow(projectManager, "wantOpenProject", true)) {
			m_wantOpenProjectPathBuffer[0] = '\0';
			OpenProjectAndLoadDefaultScene(filePath.string(), projectManager, EditorApp::Instance());
			m_selectedFolder.clear();
		}
	}
	else if (projectManager->IsProjectOpen())	// 导入资产
	{
		std::string targetWindow = GetWindowAtPosition(mousePos);

		// 文件夹内容放在 "Folder" 窗口的子窗口里，子窗口名是 "Folder/..."
		if (targetWindow == "Folder" || targetWindow.starts_with("Folder/"))
		{
			std::string importFolerPath = m_selectedFolder.empty() || m_selectedFolder == "." ?
				projectManager->GetProjectFolderFullPath()
				: projectManager->GetProjectAssetFullPath(m_selectedFolder);
			fs::path importPath = fs::path(importFolerPath) / filePath.filename().string();
			s_assetImportPopup.SetImportPath(importPath.string());// 导入到资产浏览器当前文件夹
		}

		s_assetImportPopup.SetFilePath(filePath.string());
		s_assetImportPopup.SetVisiable(true);
	}
}

void ImguiLayout::DropFolder(const std::string& folderPath, ProjectManager* projectManager, const ImVec2& mousePos)
{
	std::vector<fs::path> files;
	try {
		for (const auto& entry : fs::directory_iterator(folderPath)) {
			if (entry.is_regular_file()) {
				files.push_back(entry.path());
			}
		}
	}
	catch (...) {
		return;
	}

	bool hasOpenProject = false;
	for (auto& filePath : files)
	{
		if (filePath.extension().string() == ".project")
		{
			memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
			strcpy(m_wantOpenProjectPathBuffer, filePath.string().c_str());
			hasOpenProject = true;

			if (!RequestProjectFlow(projectManager, "wantOpenProject", true)) {
				m_wantOpenProjectPathBuffer[0] = '\0';
				OpenProjectAndLoadDefaultScene(filePath.string(), projectManager, EditorApp::Instance());
				m_selectedFolder.clear();
			}
			break;
		}
	}

	if (!hasOpenProject)
	{

		memset(m_newProjectPathBuffer, 0, sizeof(m_newProjectPathBuffer));
		strcpy(m_newProjectPathBuffer, folderPath.c_str());
		if (!RequestProjectFlow(projectManager, "new", true))
			s_openNewProjectPopup = true;
	}
}

void ImguiLayout::HandleAssetDrop(const AssetMeta& meta, EditorApp* editorApp, ProjectManager* projectManager, const ImVec2& viewportPos, const ImVec2& viewportSize)
{
	// 获取鼠标在场景中的世界坐标位置
	ImVec2 mousePos = ImGui::GetMousePos();
	//glm::vec3 worldPos = GetMouseWorldPosition(mousePos, editorApp, viewportPos, viewportSize);


	auto assetObject = projectManager->LoadAsset(meta.guid);
	if (!assetObject)
		return;

	auto GetMouseRay =
		[&](glm::vec3& rayOrigin, glm::vec3& rayDirection)
		{
			glm::mat4 view(1.f), projection(1.f);
			glm::vec3 snapPos(0.f), snapDir(0.f, 0.f, -1.f);
			editorApp->GetCameraSnapshot(view, projection, snapPos, snapDir);

			glm::mat4 projOG = projection;
			projOG[1][1] *= -1;

			// 计算射线
			ImGuizmo::ComputeMouseRay(
				(float*)&view,
				(float*)&projOG,
				mousePos,
				viewportPos,
				viewportSize,
				(float*)&rayOrigin,
				(float*)&rayDirection
			);
		};

	auto RayCast =
		[&](const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
			World& world, Entity& entity,
			glm::vec3* hitPos = nullptr, float* distance = nullptr, glm::vec3* hitNormal = nullptr
			) -> bool
		{
			auto result = editorApp->Scene().RayCast(rayOrigin, rayDirection);
			if (!result.hit)
				return false;
			if (!world.IsValidWorldEntity(result.hitEntity))
				return false;

			entity = result.hitEntity;
			if (hitPos)
				*hitPos = result.hitPoint;
			if (distance)
				*distance = result.distance;
			if (hitNormal)
				*hitNormal = result.hitNormal;

			return true;
		};


	auto RayCastFromMouse =
		[&](World& world, Entity& entity,
			glm::vec3* hitPos = nullptr, float* distance = nullptr, glm::vec3* hitNormal = nullptr
			) -> bool
		{
			// 计算射线
			glm::vec3 rayOrigin;
			glm::vec3 rayDirection;
			GetMouseRay(rayOrigin, rayDirection);
			return RayCast(rayOrigin, rayDirection, world, entity, hitPos, distance, hitNormal);
		};

	switch (meta.type)
	{
	case AssetType::StaticMesh:
	{
		if (auto meshes = assetObject->AsStaticMesh(); !meshes.empty())
		{
			auto model = std::make_shared<StaticModel>();
			for (auto mesh : meshes)
				model->AddMesh(mesh, std::make_shared<Material>());
			model->SetAssetGUID(meta.guid);

			auto entity = editorApp->Scene().CreateStaticModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				editorApp->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();
			}

		}
		break;
	}
	case AssetType::SkinnedMesh:
	{
		if (auto meshes = assetObject->AsSkinnedMesh(); !meshes.empty())
		{
			auto model = std::make_shared<SkinnedModel>();
			for (auto mesh : meshes)
				model->AddMesh(mesh, std::make_shared<Material>());
			model->SetAssetGUID(meta.guid);

			auto entity = editorApp->Scene().CreateSkinnedModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				editorApp->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();
			}

		}
		break;
	}
	case AssetType::Texture:
	{
		// 贴图拖进场景只管 3D（不转成精灵）；目前还没有给 3D 物体贴图的处理
		if (auto texture = assetObject->AsTexture())
		{
		}
		break;
	}
	case AssetType::Sprite:
	case AssetType::AnimatedImage:
	{
		// 2D：在鼠标位置（换算到当前 2D 相机下的世界坐标）建一个精灵 / 动图实体
		auto world = editorApp->GetWorld();
		if (!world)
			break;
		world->SubmitCommand([&](World& w)
			{
				const auto camera = RenderSystem::GetActiveCamera2D(w);
				const auto view = VKRenderContext::ComputeView2D(camera,
					static_cast<uint32_t>(std::max(1.0f, viewportSize.x)), static_cast<uint32_t>(std::max(1.0f, viewportSize.y)));
				const glm::vec2 position = view.ScreenToWorld(glm::vec2(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y));
				Sprite2DEditor::Create2DEntity(w, meta, position);
			}).get();
		break;
	}
	case AssetType::Material:
	{
		if (auto material = assetObject->AsMaterial())
		{
			editorApp->GetWorld()->SubmitCommand([&](World& world)
				{

					Entity entity;
					if (!RayCastFromMouse(world, entity))
						return;

					auto rModel = entity.tryGetComponent<RenderStaticModel>();
					if (!rModel || !rModel->model)
						return;
					if (rModel->model->GetMeshInfos().size() == 1)
					{
						const auto& currentMaterial = rModel->model->GetMeshInfos()[0].material;
						if (material->GetAssetGUID().empty()
							&& currentMaterial && !currentMaterial->GetAssetGUID().empty())
							return;

						auto instanceModel = rModel->model->Clone(false, false);
						auto& meshInfos = instanceModel->GetMeshInfos();
						meshInfos[0].material = material;
						rModel->model = std::move(instanceModel);
						const auto& materialGuid = material->GetAssetGUID();
						if (materialGuid.empty())
						{
							rModel->materialOverrideGUIDs.erase(0);
							rModel->materialOverrideProperties[0] = material->GetProperties();
						}
						else
						{
							rModel->materialOverrideGUIDs[0] = materialGuid;
							rModel->materialOverrideProperties.erase(0);
						}
					}

				}).get();
		}
		break;
	}
	case AssetType::StaticModel:
	{
		if (auto model = assetObject->AsStaticModel())
		{
			auto entity = editorApp->Scene().CreateStaticModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				editorApp->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();

			}
		}
		break;
	}
	case AssetType::SkinnedModel:
	{
		if (auto model = assetObject->AsSkinnedModel())
		{
			auto entity = editorApp->Scene().CreateSkinnedModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				editorApp->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();
			}
		}
		break;
	}
	case AssetType::Scene:
	{
		if (editorApp->Scene().LoadSceneAsset(meta.guid))
		{
			m_currentSceneAssetGuid = meta.guid;
			selectedEntity = Entity();
		}
		break;
	}
	case AssetType::Audio:
	{
		if (auto audio = assetObject->AsScene())
		{
		}
		break;
	}
	case AssetType::Script:
	{
		Entity entity;
		auto world = editorApp->GetWorld();
		if (world && RayCastFromMouse(*world, entity))
			ApplyScriptAsset(entity, meta, projectManager);
		break;
	}
	case AssetType::UiDocument:
	{
		Entity entity;
		auto world = editorApp->GetWorld();
		if (world && RayCastFromMouse(*world, entity))
			ApplyUIAsset(entity, meta, projectManager);
		break;
	}
	}
}

void ImguiLayout::ApplyScriptAsset(Entity target, const AssetMeta& meta, ProjectManager* projectManager)
{
	if (!target || !projectManager || meta.type != AssetType::Script)
		return;
	auto asset = projectManager->LoadAsset(meta.guid);
	auto source = asset ? asset->AsScript() : nullptr;
	if (!source)
		return;
	auto world = target.getWorld();
	if (!world)
		return;
	auto assign = [target, source](World& activeWorld) {
		if (!activeWorld.IsValidWorldEntity(target))
			return;
		auto script = target.tryGetComponent<Script>();
		if (!script)
			script = &target.addComponent<Script>();
		script->data = source;
		script->needReload = true;
		};
	if (world->isRunning())
		world->SubmitCommand(assign).get();
	else
		assign(*world);
}

void ImguiLayout::ApplyUIAsset(Entity target, const AssetMeta& meta, ProjectManager* projectManager)
{
	if (!target || !projectManager || meta.type != AssetType::UiDocument)
		return;

	auto world = target.getWorld();
	if (!world)
		return;

	const AssetGUID guid = meta.guid;

	// 新挂上的 UIView 给个默认视图名（main.ui → "main"），模块就能直接 FindDoc；
	// 已有的视图名不动，想改在属性面板里改。
	std::string defaultViewName = meta.name;
	if (defaultViewName.size() > 3)
	{
		std::string tail = defaultViewName.substr(defaultViewName.size() - 3);
		std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (tail == ".ui")
			defaultViewName.resize(defaultViewName.size() - 3);
	}

	auto assign = [target, guid, defaultViewName](World& activeWorld) {
		if (!activeWorld.IsValidWorldEntity(target))
			return;
		auto view = target.tryGetComponent<UIView>();
		if (!view)
			view = &target.addComponent<UIView>();
		view->documentGuid = guid;
		if (view->viewName.empty())
			view->viewName = defaultViewName;
		view->needReload = true;
		};

	if (world->isRunning())
		world->SubmitCommand(assign).get();
	else
		assign(*world);
}

void ImguiLayout::HandleUIAssetFileDrop(const std::string& filePath, Entity target, ProjectManager* projectManager)
{
	if (filePath.empty() || !target || !projectManager || !projectManager->IsProjectOpen())
		return;

	std::error_code ec;
	fs::path fullPath = fs::weakly_canonical(filePath, ec);
	if (ec)
		fullPath = fs::absolute(filePath, ec).lexically_normal();

	fs::path destination = fullPath;
	fs::path projectPath = fs::weakly_canonical(projectManager->GetProjectFolderFullPath(), ec);
	fs::path relativePath = fullPath.lexically_relative(projectPath);
	if (ec || relativePath.empty() || relativePath.is_absolute() || *relativePath.begin() == "..")
		destination = projectPath / "Assets" / "Others" / fullPath.filename();

	s_assetImportPopup.SetFilePath(fullPath.string());
	s_assetImportPopup.SetImportPath(destination.string());
	s_assetImportPopup.SetImportCallback([target, projectManager](const std::vector<AssetMeta>& importedMetas) {
		for (const auto& importedMeta : importedMetas)
			ApplyUIAsset(target, importedMeta, projectManager);
		});
	s_assetImportPopup.SetVisiable(true);
}

void ImguiLayout::HandleScriptFileDrop(const std::string& filePath, Entity target, ProjectManager* projectManager)
{
	if (filePath.empty() || !target || !projectManager || !projectManager->IsProjectOpen())
		return;

	std::error_code ec;
	fs::path fullPath = fs::weakly_canonical(filePath, ec);
	if (ec)
		fullPath = fs::absolute(filePath, ec).lexically_normal();

	fs::path destination = fullPath;
	fs::path projectPath = fs::weakly_canonical(projectManager->GetProjectFolderFullPath(), ec);
	fs::path relativePath = fullPath.lexically_relative(projectPath);
	if (ec || relativePath.empty() || relativePath.is_absolute() || *relativePath.begin() == "..")
		destination = projectPath / "Assets" / "Others" / fullPath.filename();

	s_assetImportPopup.SetFilePath(fullPath.string());
	s_assetImportPopup.SetImportPath(destination.string());
	s_assetImportPopup.SetImportCallback([target, projectManager](const std::vector<AssetMeta>& importedMetas) {
		for (const auto& importedMeta : importedMetas)
			ApplyScriptAsset(target, importedMeta, projectManager);
		});
	s_assetImportPopup.SetVisiable(true);
}

namespace
{
	// 转资产前发现同一张图已经有同类资产时的确认框
	struct ImageConvertConfirm
	{
		bool pending = false;
		std::string title;					// 例如"从贴图 xxx 生成精灵"
		std::vector<AssetMeta> existing;		// 已有的同类资产
		std::function<void()> proceed;			// 仍然新建：打开导入弹窗
		std::function<void(const AssetMeta&)> useExisting;	// 用已有的（可为空：只是生成资产时没有"用"这个说法）
	};
	ImageConvertConfirm s_imageConvertConfirm;

	// 导入回调里挑出指定类型的那个新资产交给调用方
	std::function<void(const std::vector<AssetMeta>&)> PickCreated(AssetType type, std::function<void(const AssetMeta&)> onCreated)
	{
		return [type, onCreated = std::move(onCreated)](const std::vector<AssetMeta>& metas)
			{
				if (!onCreated)
					return;
				for (const auto& created : metas)
				{
					if (created.type == type)
					{
						onCreated(created);
						return;
					}
				}
			};
	}
}

bool ImguiLayout::RequestSpriteFromTexture(const AssetGUID& textureGuid, ProjectManager* projectManager, std::function<void(const AssetMeta&)> onCreated)
{
	if (!projectManager || !projectManager->IsProjectOpen())
		return false;
	auto database = projectManager->GetDatabase();
	auto textureMeta = database ? database->GetAssetMetaByGuid(textureGuid) : nullptr;
	if (!textureMeta || textureMeta->type != AssetType::Texture)
		return false;

	TextureAssetDescription desc;
	if (!desc.LoadFromFile(projectManager->GetProjectAssetFullPath(textureMeta->path)) || desc.GetPath().empty())
		return false;

	// 原图已经在工程里：源路径和目标路径相同，导入时不会再复制一份
	const std::string source = projectManager->GetProjectAssetFullPath(desc.GetPath());
	const TextureConfig config = desc.GetConfig();
	const std::string textureName = textureMeta->name;
	auto openImport = [source, config, textureName, onCreated]()
		{
			s_assetImportPopup.SetFilePath(source);
			s_assetImportPopup.SetImportPath(source);
			s_assetImportPopup.SetAssetType(AssetType::Sprite);
			s_assetImportPopup.SetImage2DConfig(config);
			s_assetImportPopup.SetHint(std::format("从贴图 {} 生成精灵：用同一张原图，导入配置默认取这张贴图的", textureName));
			s_assetImportPopup.SetImportCallback(PickCreated(AssetType::Sprite, onCreated));
			s_assetImportPopup.SetVisiable(true);
		};

	auto existing = projectManager->FindImageAssetsFromSource(AssetType::Sprite, desc.GetPath());
	if (existing.empty())
	{
		openImport();
		return true;
	}
	s_imageConvertConfirm.pending = true;
	s_imageConvertConfirm.title = std::format("从贴图 {} 生成精灵", textureName);
	s_imageConvertConfirm.existing = std::move(existing);
	s_imageConvertConfirm.proceed = openImport;
	s_imageConvertConfirm.useExisting = onCreated;
	return true;
}

bool ImguiLayout::RequestTextureFromSprite(const AssetGUID& spriteGuid, ProjectManager* projectManager, std::function<void(const AssetMeta&)> onCreated)
{
	if (!projectManager || !projectManager->IsProjectOpen())
		return false;
	auto database = projectManager->GetDatabase();
	auto spriteMeta = database ? database->GetAssetMetaByGuid(spriteGuid) : nullptr;
	if (!spriteMeta || spriteMeta->type != AssetType::Sprite)
		return false;

	SpriteAssetDescription desc;
	if (!desc.LoadFromFile(projectManager->GetProjectAssetFullPath(spriteMeta->path)) || desc.GetPath().empty())
		return false;

	// 精灵只存了过滤方式和 sRGB；贴图的其它配置（环绕、各向异性）用默认值，可以在弹窗里改
	const TextureConfig spriteConfig = desc.GetConfig();
	TextureConfig config;
	config.minFilter = spriteConfig.minFilter;
	config.magFilter = spriteConfig.magFilter;
	config.gammaCorrection = spriteConfig.gammaCorrection;

	const std::string source = projectManager->GetProjectAssetFullPath(desc.GetPath());
	const std::string spriteName = spriteMeta->name;
	auto openImport = [source, config, spriteName, onCreated]()
		{
			s_assetImportPopup.SetFilePath(source);
			s_assetImportPopup.SetImportPath(source);
			s_assetImportPopup.SetAssetType(AssetType::Texture);
			s_assetImportPopup.SetTextureConfig(config);
			s_assetImportPopup.SetHint(std::format("从精灵 {} 生成贴图：用同一张原图，过滤方式和 Gamma 默认取这个精灵的", spriteName));
			s_assetImportPopup.SetImportCallback(PickCreated(AssetType::Texture, onCreated));
			s_assetImportPopup.SetVisiable(true);
		};

	auto existing = projectManager->FindImageAssetsFromSource(AssetType::Texture, desc.GetPath());
	if (existing.empty())
	{
		openImport();
		return true;
	}
	s_imageConvertConfirm.pending = true;
	s_imageConvertConfirm.title = std::format("从精灵 {} 生成贴图", spriteName);
	s_imageConvertConfirm.existing = std::move(existing);
	s_imageConvertConfirm.proceed = openImport;
	s_imageConvertConfirm.useExisting = onCreated;
	return true;
}

void ImguiLayout::DrawImageConvertConfirm()
{
	auto& state = s_imageConvertConfirm;
	if (!state.pending)
		return;

	auto finish = [&](std::function<void()> action)
		{
			// 先清状态再执行：执行里会打开导入弹窗，或者把资产填回去
			state = ImageConvertConfirm{};
			ImGui::CloseCurrentPopup();
			if (action)
				action();
		};

	ImGui::OpenPopup("ImageConvertConfirm");
	if (ImGui::BeginPopupModal("ImageConvertConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(state.title.c_str());
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "这张图已经有 %zu 个同类资产：", state.existing.size());
		for (size_t i = 0; i < state.existing.size(); ++i)
		{
			const AssetMeta meta = state.existing[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::BulletText("%s", meta.name.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", meta.path.c_str());
			if (state.useExisting)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("用这个"))
				{
					auto use = state.useExisting;
					finish([use, meta]() { use(meta); });
					ImGui::PopID();
					ImGui::EndPopup();
					return;
				}
			}
			ImGui::PopID();
		}
		ImGui::Spacing();
		if (ImGui::Button("仍然新建"))
		{
			finish(state.proceed);
			ImGui::EndPopup();
			return;
		}
		ImGui::SameLine();
		if (ImGui::Button("取消"))
			finish({});
		ImGui::EndPopup();
	}
}

std::string ImguiLayout::GetWindowAtPosition(const ImVec2& pos)
{
	ImGuiContext& g = *ImGui::GetCurrentContext();

	// 从后向前遍历（最上层的窗口优先）
	for (int i = g.Windows.Size - 1; i >= 0; --i) {
		ImGuiWindow* window = g.Windows[i];

		// 跳过隐藏/折叠的窗口
		if (!window->Active || window->Collapsed) {
			continue;
		}

		// 跳过主菜单栏和状态栏等特殊窗口
		if (window->Flags & (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking)) {}

		// 检查鼠标是否在窗口区域内
		ImRect rect(window->Pos, window->Pos + window->Size);
		if (rect.Contains(pos)) {
			return window->Name;
		}
	}

	return "";
}
