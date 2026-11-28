#include "ImguiLayout.h"

#include "Helper/FileIO.h"
#include "glm/gtc/type_ptr.hpp"
#include "CommonComponent.h"
#include "PropertiesHelper.h"
#include "DDGIEditor.h"
#include "ViewportIcons.h"
#include "PhysicsDebugView.h"
#include "PhysicsShapeFromModel.h"
#include "RenderProfilerPanel.h"
#include "LogicProfilerPanel.h"
#include "RenderOptionPanels.h"
#include "IconManager.h"
#include "Sprite2DEditor.h"
#include "TextAssetEditor.h"
#include "AssetThumbnails.h"
#include "MaterialPreview.h"
#include "Render/Systems/RenderSystem.h"
#include "RenderEngine/Renderer3D.h"
#include "VulkanRenderEngine/General/EditorViewport.h"
#include "PlatformRuntimeComponents.h"
#include "SceneCodec/ComponentCodec.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <array>
#include <future>
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

	// ---------------- 选中集合（多选 / 框选） ----------------
	// ImguiLayout::selectedEntity 始终是选中列表里的第一个（DDGI、阴影调试、单选时的手柄 / 属性面板等照旧只看它）；
	// 完整列表按加入顺序放在 s_selection 里。别处直接给 selectedEntity 赋值（删除、复制、切场景等）时，
	// SyncSelection 发现两者对不上，就把列表重置成只有它一个。
	// 多选只支持用手柄一起调整变换：属性面板只读、只显示变换组件，删除 / 复制在多选时不可用
	enum class SelectMode { Replace, Add, Toggle };

	std::vector<Entity> s_selection;
	uint64_t s_selectionVersion = 1;		// 选中每变一次 +1：异步拾取的结果据此判断还能不能用
	Entity s_hierarchyRangeAnchor;			// 层级面板 Shift 区间选的起点：上一次不带 Shift 点的那一行

	// 按下时的修饰键：Ctrl = 切换，Shift = 加入，否则替换
	SelectMode SelectModeFromKeys()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl)
			return SelectMode::Toggle;
		if (io.KeyShift)
			return SelectMode::Add;
		return SelectMode::Replace;
	}

	bool IsSelected(Entity entity)
	{
		return entity && std::find(s_selection.begin(), s_selection.end(), entity) != s_selection.end();
	}

	void SetSelectionList(const std::vector<Entity>& list)
	{
		std::vector<Entity> unique;
		unique.reserve(list.size());
		for (const Entity& e : list)
		{
			if (e && std::find(unique.begin(), unique.end(), e) == unique.end())
				unique.push_back(e);
		}
		const Entity first = unique.empty() ? Entity() : unique.front();
		if (unique == s_selection && ImguiLayout::selectedEntity == first)
			return;
		s_selection = std::move(unique);
		ImguiLayout::selectedEntity = first;
		++s_selectionVersion;
	}

	// 每帧用选中之前调一次：吸收别处对 selectedEntity 的直接赋值，去掉已经销毁的实体
	void SyncSelection(World* world)
	{
		const Entity first = s_selection.empty() ? Entity() : s_selection.front();
		if (ImguiLayout::selectedEntity != first)
		{
			s_selection.clear();
			if (ImguiLayout::selectedEntity)
				s_selection.push_back(ImguiLayout::selectedEntity);
			++s_selectionVersion;
		}
		if (!world)
			return;
		const size_t before = s_selection.size();
		std::erase_if(s_selection, [&](const Entity& e) { return !e || !world->IsValidWorldEntity(e); });
		if (s_selection.size() != before)
		{
			ImguiLayout::selectedEntity = s_selection.empty() ? Entity() : s_selection.front();
			++s_selectionVersion;
		}
	}

	// picked 按顺序排；Replace 时原来的第一个也在 picked 里就让它继续排第一（框选后手柄不跳到别的物体上）
	void ApplySelection(const std::vector<Entity>& picked, SelectMode mode)
	{
		std::vector<Entity> list;
		switch (mode)
		{
		case SelectMode::Replace:
		{
			const Entity first = s_selection.empty() ? Entity() : s_selection.front();
			if (first && std::find(picked.begin(), picked.end(), first) != picked.end())
				list.push_back(first);
			list.insert(list.end(), picked.begin(), picked.end());
			break;
		}
		case SelectMode::Add:
			list = s_selection;
			list.insert(list.end(), picked.begin(), picked.end());
			break;
		case SelectMode::Toggle:
			list = s_selection;
			for (const Entity& e : picked)
			{
				auto it = std::find(list.begin(), list.end(), e);
				if (it != list.end())
					list.erase(it);
				else
					list.push_back(e);
			}
			break;
		}
		SetSelectionList(list);
	}

	// ---------------- 多选删除（确认后执行） ----------------
	// 编辑器没有撤销，多选时右键菜单的 Delete 不直接删，先弹确认框；单选时照旧直接删。
	// 请求时把当前选中拍个快照，确认框在 DrawSceneView 末尾（所有窗口之外）打开和绘制，层级面板、视口的菜单共用一个
	struct MultiDeleteConfirm
	{
		bool requested = false;			// 本帧要打开确认框
		std::vector<Entity> entities;	// 要删除的实体（请求时的选中）
	};
	MultiDeleteConfirm s_multiDelete;
	constexpr const char* kMultiDeletePopup = "删除实体##ConfirmMultiDelete";
	// 确认框里最多列出多少个名字，其余的只显示数量
	constexpr size_t kMultiDeleteListMax = 10;

	void RequestMultiDelete()
	{
		s_multiDelete.requested = true;
		s_multiDelete.entities = s_selection;
	}

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

	// ---------------- 手柄（单选 / 多选） ----------------
	// 只选一个时保持原来的规则（Use2DGizmo：从 2D 层点选到的、或者只有 Transform2D 的用 2D 手柄，否则用 3D 手柄）。
	// 多选时：只要有一个实体有 Transform 就调 Transform（3D 手柄），否则有 Transform2D 就调 Transform2D（2D 手柄）；
	// 手柄放在列表里第一个有对应组件的实体（anchor）上、跟着它走，属性面板也显示它；
	// 改动作用于所有有对应组件的选中实体，没有对应组件的不受影响
	struct GizmoTargets
	{
		bool is3D = false;
		bool is2D = false;
		Entity anchor;
		std::vector<Entity> entities;		// 有对应组件的选中实体，第一个是 anchor
	};

	GizmoTargets ResolveGizmoTargets()
	{
		GizmoTargets targets;
		if (s_selection.size() <= 1)
		{
			const Entity e = ImguiLayout::selectedEntity;
			if (!e || !e.isValid())
				return targets;
			if (Use2DGizmo(e))
				targets.is2D = true;
			else if (e.tryGetComponent<Transform>())
				targets.is3D = true;
			else
				return targets;
			targets.anchor = e;
			targets.entities.push_back(e);
			return targets;
		}

		for (const Entity& e : s_selection)
		{
			if (e.isValid() && e.tryGetComponent<Transform>())
				targets.entities.push_back(e);
		}
		if (!targets.entities.empty())
			targets.is3D = true;
		else
		{
			for (const Entity& e : s_selection)
			{
				if (e.isValid() && e.tryGetComponent<Transform2D>())
					targets.entities.push_back(e);
			}
			targets.is2D = !targets.entities.empty();
		}
		if (!targets.entities.empty())
			targets.anchor = targets.entities.front();
		return targets;
	}

	// ── 2D 手柄 ──
	// 拖动开始时记下每个实体的值；之后每帧按 anchor 相对开始时的变化，从各自的开始值算新值（绝对赋值，不累积误差）：
	//   平移：加同样的位移；旋转：绕 anchor 的位置转同样的角度（位置跟着绕，各自的角度也加上）；缩放：各自的缩放乘同样的比例
	struct Transform2DState
	{
		Entity entity;
		glm::vec2 position{ 0.0f };
		float rotation = 0.0f;
		glm::vec2 scale{ 1.0f };
	};

	struct Gizmo2DDrag
	{
		bool active = false;
		Transform2DState anchorStart;
		Transform2DState current;				// 拖动中 anchor 的值（手柄用它，不等场景线程写回）
		std::vector<Transform2DState> starts;	// 其它实体拖动开始时的值
	};
	Gizmo2DDrag s_gizmo2DDrag;

	void Draw2DGizmo(EditorApp* app, const GizmoTargets& targets, const ImVec2& viewportSize, ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, const float* snap)
	{
		World* world = app ? app->GetWorld() : nullptr;
		const Entity anchor = targets.anchor;
		auto* transform = anchor ? anchor.tryGetComponent<Transform2D>() : nullptr;
		if (!world || !transform)
		{
			s_gizmo2DDrag.active = false;
			return;
		}

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

		const bool continuing = s_gizmo2DDrag.active && s_gizmo2DDrag.anchorStart.entity == anchor;
		const Transform2DState cur = continuing ? s_gizmo2DDrag.current
			: Transform2DState{ anchor, transform->position, transform->rotation, transform->scale };
		const glm::vec2 position = cur.position;
		const float rotation = cur.rotation;
		const glm::vec2 scale = cur.scale;
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
		{
			s_gizmo2DDrag.active = false;
			return;
		}

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

		if (!continuing)
		{
			s_gizmo2DDrag.active = true;
			s_gizmo2DDrag.anchorStart = cur;
			s_gizmo2DDrag.starts.clear();
			for (const Entity& e : targets.entities)
			{
				if (e == anchor)
					continue;
				if (auto* t = e.tryGetComponent<Transform2D>())
					s_gizmo2DDrag.starts.push_back({ e, t->position, t->rotation, t->scale });
			}
		}
		s_gizmo2DDrag.current = { anchor, newPosition, newRotation, newScale };

		const Transform2DState& a0 = s_gizmo2DDrag.anchorStart;
		std::vector<Transform2DState> results;
		results.reserve(s_gizmo2DDrag.starts.size());
		for (const auto& s : s_gizmo2DDrag.starts)
		{
			Transform2DState r = s;
			if (operation == ImGuizmo::ROTATE)
			{
				// 和 Transform2D 的旋转同一个方向约定（x 轴 (1, 0) 转到 (cos, sin)）
				const float delta = newRotation - a0.rotation;
				const float c = std::cos(glm::radians(delta));
				const float sn = std::sin(glm::radians(delta));
				const glm::vec2 o = s.position - a0.position;
				r.position = a0.position + glm::vec2(c * o.x - sn * o.y, sn * o.x + c * o.y);
				r.rotation = s.rotation + delta;
			}
			else if (operation == ImGuizmo::SCALE)
			{
				for (int k = 0; k < 2; ++k)
				{
					if (std::abs(a0.scale[k]) > 1e-6f)
						r.scale[k] = s.scale[k] * (newScale[k] / a0.scale[k]);
				}
			}
			else
				r.position = s.position + (newPosition - a0.position);
			results.push_back(r);
		}

		world->SubmitCommand([anchorState = s_gizmo2DDrag.current, results = std::move(results)](World& w) mutable
			{
				auto apply = [&w](const Transform2DState& s)
					{
						if (!w.IsValidWorldEntity(s.entity))
							return;
						if (auto* t = s.entity.tryGetComponent<Transform2D>())
						{
							t->position = s.position;
							t->rotation = s.rotation;
							t->scale = s.scale;
						}
					};
				apply(anchorState);
				for (const auto& r : results)
					apply(r);
			});
	}

	// ── 3D 手柄 ──
	// 和 2D 一样：拖动开始时记下每个实体的值，之后按 anchor 相对开始时的变化从各自的开始值算新值：
	//   平移：加同样的世界位移；旋转：绕 anchor 的位置转同样的世界旋转；缩放：各自的缩放乘同样的（anchor 局部轴上的）比例。
	// 变化量直接从 anchor 前后两个矩阵算（列向量），不走四元数分解
	struct TransformState
	{
		Entity entity;
		glm::vec3 position{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f };
	};

	struct Gizmo3DDrag
	{
		bool active = false;
		Entity anchor;
		glm::mat4 startMatrix{ 1.0f };			// anchor 拖动开始时的矩阵
		glm::mat4 current{ 1.0f };				// 拖动中 anchor 的矩阵（手柄用它，不等场景线程写回）
		std::vector<TransformState> starts;		// 其它实体拖动开始时的值
		std::vector<Entity> entities;			// 这次拖动改到的全部实体（含 anchor）
	};
	Gizmo3DDrag s_gizmo3DDrag;

	// 拖动结束：DDGI 体积把缩放折算成探针数、中心吸附到网格（原来只对单个选中做，现在对每个被拖动的体积做）
	void FinishGizmo3DDrag()
	{
		if (!s_gizmo3DDrag.active)
			return;
		for (const Entity& e : s_gizmo3DDrag.entities)
		{
			if (DDGIEditor::IsVolume(e))
				DDGIEditor::FinalizeVolumeTransform(e);
		}
		s_gizmo3DDrag = Gizmo3DDrag{};
	}

	// 去掉缩放的旋转部分（列归一化；和 Transform::SetMatrix 的 decompose 一样不支持负缩放的镜像）
	glm::mat3 RotationPart(const glm::mat4& m)
	{
		glm::mat3 r(m);
		for (int i = 0; i < 3; ++i)
		{
			const float len = glm::length(r[i]);
			if (len > 1e-8f)
				r[i] /= len;
		}
		return r;
	}

	// 改了 Transform 的物理实体要同步到刚体并唤醒（场景线程上调用）
	void SyncPhysicsTransform(Entity entity)
	{
		auto* physics = entity.tryGetComponent<Physics>();
		auto* runtime = entity.tryGetComponent<PhysicsRuntime>();
		if (!physics || !runtime)
			return;
		physics->forceSyncTransform = true;
		if (runtime->body && !runtime->body->isActive())
			runtime->body->activate(true);
		if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
			runtime->ghostObject->activate(true);
	}

	void Draw3DGizmo(EditorApp* app, const GizmoTargets& targets, const glm::mat4& view, const glm::mat4& projection,
		ImGuizmo::OPERATION operation, ImGuizmo::MODE mode, const float* snap)
	{
		World* world = app ? app->GetWorld() : nullptr;
		const Entity anchor = targets.anchor;
		auto* transform = anchor ? anchor.tryGetComponent<Transform>() : nullptr;
		if (!world || !transform)
		{
			FinishGizmo3DDrag();
			return;
		}

		const bool continuing = s_gizmo3DDrag.active && s_gizmo3DDrag.anchor == anchor;
		glm::mat4 matrix = continuing ? s_gizmo3DDrag.current : transform->GetMatrix();
		const glm::mat4 before = matrix;

		glm::mat4 projOG = projection;
		projOG[1][1] *= -1;
		glm::mat4 deltaMatrix(1.0f);

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::Manipulate(
			(float*)glm::value_ptr(view),
			(float*)glm::value_ptr(projOG),
			operation,
			mode,
			(float*)glm::value_ptr(matrix),
			(float*)glm::value_ptr(deltaMatrix),
			snap
		);

		if (!ImGuizmo::IsUsing())
		{
			FinishGizmo3DDrag();
			return;
		}

		if (!continuing)
		{
			FinishGizmo3DDrag();
			s_gizmo3DDrag.active = true;
			s_gizmo3DDrag.anchor = anchor;
			s_gizmo3DDrag.startMatrix = before;
			s_gizmo3DDrag.entities = targets.entities;
			for (const Entity& e : targets.entities)
			{
				if (e == anchor)
					continue;
				if (auto* t = e.tryGetComponent<Transform>())
					s_gizmo3DDrag.starts.push_back({ e, t->position, t->rotation, t->scale });
			}
		}
		s_gizmo3DDrag.current = matrix;

		std::vector<TransformState> results;
		if (!s_gizmo3DDrag.starts.empty())
		{
			const glm::mat4& m0 = s_gizmo3DDrag.startMatrix;
			const glm::vec3 p0(m0[3]);
			const glm::vec3 pN(matrix[3]);
			glm::mat3 dR(1.0f);
			glm::quat dq(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec3 ratio(1.0f);
			if (operation == ImGuizmo::ROTATE)
			{
				dR = RotationPart(matrix) * glm::transpose(RotationPart(m0));
				dq = glm::normalize(glm::quat_cast(dR));
			}
			else if (operation == ImGuizmo::SCALE)
			{
				for (int k = 0; k < 3; ++k)
				{
					const float len0 = glm::length(glm::vec3(m0[k]));
					if (len0 > 1e-6f)
						ratio[k] = glm::length(glm::vec3(matrix[k])) / len0;
				}
			}

			results.reserve(s_gizmo3DDrag.starts.size());
			for (const auto& s : s_gizmo3DDrag.starts)
			{
				TransformState r = s;
				if (operation == ImGuizmo::ROTATE)
				{
					r.position = p0 + dR * (s.position - p0);
					r.rotation = glm::normalize(dq * s.rotation);
				}
				else if (operation == ImGuizmo::SCALE)
					r.scale = s.scale * ratio;
				else
					r.position = s.position + (pN - p0);
				results.push_back(r);
			}
		}

		world->SubmitCommand([anchor, matrix, results = std::move(results)](World& w) mutable
			{
				if (w.IsValidWorldEntity(anchor))
				{
					if (auto* t = anchor.tryGetComponent<Transform>())
					{
						t->SetMatrix(matrix);
						SyncPhysicsTransform(anchor);
					}
				}
				for (const auto& r : results)
				{
					if (!w.IsValidWorldEntity(r.entity))
						continue;
					if (auto* t = r.entity.tryGetComponent<Transform>())
					{
						t->position = r.position;
						t->rotation = r.rotation;
						t->scale = r.scale;
						SyncPhysicsTransform(r.entity);
					}
				}
			});
	}

	// ---------------- 选中物体的包围盒（2D 矩形 / 3D OBB） ----------------
	// 青色：和选中描边、图标选中圈（橙色）区分开
	constexpr ImU32 kBoundsColor = IM_COL32(70, 200, 255, 255);
	constexpr ImU32 kBoundsShadow = IM_COL32(0, 0, 0, 160);

	// 视口工具栏的「包围盒」开关（2D 和 3D 一起）
	bool s_showSelectionBounds = false;

	// 多选时最多给多少个实体画包围盒（每帧都要在场景线程算一遍）
	constexpr size_t kMaxBoundsEntities = 256;

	// ---------------- 选中的 2D 精灵 / 动图的包围盒 ----------------

	// 和 2D 手柄的相机一样异步取：场景线程算好当前帧的四个角（动图每帧尺寸 / 轴心可能变），界面用上一份结果
	struct Selection2DBoundsCache
	{
		std::mutex mutex;
		std::vector<Entity> entities;					// 这份结果是给哪些实体算的
		std::vector<RenderSystem::Bounds2D> bounds;		// 只有算得出来的（精灵 / 动图）
		VKRenderContext::Camera2DData camera;
		bool valid = false;
	};
	Selection2DBoundsCache s_selection2DBounds;
	std::future<void> s_selection2DBoundsTask;		// 上一次请求没执行完就不再提交

	// 画在 Scene View 窗口的绘制列表上（在手柄之前调用，手柄盖在上面），裁剪到视口矩形
	void DrawSelection2DBounds(EditorApp* app, const std::vector<Entity>& selection, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || selection.empty() || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;
		const std::vector<Entity> entities(selection.begin(), selection.begin() + std::min(selection.size(), kMaxBoundsEntities));

		using namespace std::chrono_literals;
		if (!s_selection2DBoundsTask.valid() || s_selection2DBoundsTask.wait_for(0s) == std::future_status::ready)
		{
			s_selection2DBoundsTask = world->SubmitCommand([entities](World& w)
				{
					std::vector<RenderSystem::Bounds2D> bounds;
					for (const Entity& e : entities)
					{
						RenderSystem::Bounds2D b;
						if (RenderSystem::Get2DBounds(w, e, b))
							bounds.push_back(b);
					}
					const auto camera = RenderSystem::GetActiveCamera2D(w);
					std::lock_guard lock(s_selection2DBounds.mutex);
					s_selection2DBounds.entities = entities;
					s_selection2DBounds.bounds = std::move(bounds);
					s_selection2DBounds.camera = camera;
					s_selection2DBounds.valid = true;
				});
		}

		std::vector<RenderSystem::Bounds2D> bounds;
		VKRenderContext::Camera2DData camera;
		{
			std::lock_guard lock(s_selection2DBounds.mutex);
			// 刚改了选中时缓存还是上一份选中的，不画，等下一帧
			if (!s_selection2DBounds.valid || s_selection2DBounds.entities != entities)
				return;
			bounds = s_selection2DBounds.bounds;
			camera = s_selection2DBounds.camera;
		}
		if (bounds.empty())
			return;

		// 2D 世界 -> 渲染目标像素 -> 视口屏幕坐标（与 Pick2DAtMouse 的换算互逆）
		const auto view = Viewport2DView(camera, app, viewportSize);
		const glm::vec2 target = glm::max(view.targetSize, glm::vec2(1.0f));
		auto toScreen = [&](const glm::vec2& p)
			{
				const glm::vec2 pixel = view.WorldToScreen(p);
				return ImVec2(viewportPos.x + pixel.x / target.x * viewportSize.x,
					viewportPos.y + pixel.y / target.y * viewportSize.y);
			};

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
		for (const auto& b : bounds)
		{
			ImVec2 pts[4];
			for (int i = 0; i < 4; ++i)
				pts[i] = toScreen(b.corners[i]);
			const ImVec2 pivot = toScreen(b.pivot);
			// 先画一圈深色描边再画亮色线，浅色 / 深色背景上都看得清
			drawList->AddPolyline(pts, 4, kBoundsShadow, ImDrawFlags_Closed, 3.0f);
			drawList->AddPolyline(pts, 4, kBoundsColor, ImDrawFlags_Closed, 1.5f);
			for (const ImVec2& p : pts)
				drawList->AddRectFilled(ImVec2(p.x - 2.5f, p.y - 2.5f), ImVec2(p.x + 2.5f, p.y + 2.5f), kBoundsColor);
			drawList->AddCircle(pivot, 4.0f, IM_COL32(255, 255, 255, 230), 12, 1.5f);
		}
		drawList->PopClipRect();
	}

	// 3D：选中的静态模型（RenderStaticModel）画 OBB 线框——整个模型的局部 AABB（所有网格合并）乘上渲染用的矩阵，
	// 跟着物体旋转 / 缩放。和 2D 一样在场景线程算角点、界面用上一份结果；只画线不做深度测试（被挡住也看得见）
	struct Selection3DBoundsCache
	{
		std::mutex mutex;
		std::vector<Entity> entities;							// 这份结果是给哪些实体算的
		std::vector<std::array<glm::vec3, 8>> boxes;			// 只有静态模型的；下标的第 0 / 1 / 2 位分别选 x / y / z 的 min / max
		bool valid = false;
	};
	Selection3DBoundsCache s_selection3DBounds;
	std::future<void> s_selection3DBoundsTask;		// 上一次请求没执行完就不再提交

	// 场景线程上调用
	bool Get3DModelBounds(World& world, Entity entity, std::array<glm::vec3, 8>& out)
	{
		if (!entity || !world.IsValidWorldEntity(entity))
			return false;
		const auto* transform = entity.tryGetComponent<Transform>();
		const auto* renderModel = entity.tryGetComponent<RenderStaticModel>();
		if (!transform || !renderModel || !renderModel->model)
			return false;

		const auto aabb = renderModel->model->GetAABB();
		if (!aabb.isValid())
			return false;

		// 和 RenderSystem::ProcessStaticModel 用同一个矩阵
		const glm::mat4 model = transform->GetMatrix() * renderModel->trans;
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 local((i & 1) ? aabb.max.x : aabb.min.x, (i & 2) ? aabb.max.y : aabb.min.y, (i & 4) ? aabb.max.z : aabb.min.z);
			out[i] = glm::vec3(model * glm::vec4(local, 1.0f));
		}
		return true;
	}

	// viewProjGL：投影已翻回 GL 约定（projection[1][1] *= -1，同视口图标 / DDGIEditor）
	void DrawSelection3DBounds(EditorApp* app, const std::vector<Entity>& selection, const glm::mat4& viewProjGL, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || selection.empty() || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;
		const std::vector<Entity> entities(selection.begin(), selection.begin() + std::min(selection.size(), kMaxBoundsEntities));

		using namespace std::chrono_literals;
		if (!s_selection3DBoundsTask.valid() || s_selection3DBoundsTask.wait_for(0s) == std::future_status::ready)
		{
			s_selection3DBoundsTask = world->SubmitCommand([entities](World& w)
				{
					std::vector<std::array<glm::vec3, 8>> boxes;
					for (const Entity& e : entities)
					{
						std::array<glm::vec3, 8> corners{};
						if (Get3DModelBounds(w, e, corners))
							boxes.push_back(corners);
					}
					std::lock_guard lock(s_selection3DBounds.mutex);
					s_selection3DBounds.entities = entities;
					s_selection3DBounds.boxes = std::move(boxes);
					s_selection3DBounds.valid = true;
				});
		}

		std::vector<std::array<glm::vec3, 8>> boxes;
		{
			std::lock_guard lock(s_selection3DBounds.mutex);
			// 刚改了选中时缓存还是上一份选中的，不画，等下一帧
			if (!s_selection3DBounds.valid || s_selection3DBounds.entities != entities)
				return;
			boxes = s_selection3DBounds.boxes;
		}
		if (boxes.empty())
			return;

		auto toScreen = [&](const glm::vec4& clip)
			{
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				return ImVec2(viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x, viewportPos.y + (0.5f - ndc.y * 0.5f) * viewportSize.y);
			};

		// 12 条棱：两个角点的下标只差一位。一端在相机后面时先在裁剪空间截到 w = eps 再投影（同 DDGIEditor 的 Projector::Line）
		std::vector<std::pair<ImVec2, ImVec2>> lines;
		lines.reserve(boxes.size() * 12);
		constexpr float eps = 1e-3f;
		for (const auto& corners : boxes)
		{
			std::array<glm::vec4, 8> clip;
			for (int i = 0; i < 8; ++i)
				clip[i] = viewProjGL * glm::vec4(corners[i], 1.0f);
			for (int i = 0; i < 8; ++i)
			{
				for (int bit = 1; bit < 8; bit <<= 1)
				{
					if (i & bit)
						continue;
					glm::vec4 a = clip[i];
					glm::vec4 b = clip[i | bit];
					if (a.w <= eps && b.w <= eps)
						continue;
					if (a.w <= eps)
						a = glm::mix(a, b, (eps - a.w) / (b.w - a.w));
					else if (b.w <= eps)
						b = glm::mix(b, a, (eps - b.w) / (a.w - b.w));
					lines.push_back({ toScreen(a), toScreen(b) });
				}
			}
		}
		if (lines.empty())
			return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
		// 先画一圈深色再画亮色线，浅色 / 深色背景上都看得清
		for (const auto& line : lines)
			drawList->AddLine(line.first, line.second, kBoundsShadow, 3.0f);
		for (const auto& line : lines)
			drawList->AddLine(line.first, line.second, kBoundsColor, 1.5f);
		drawList->PopClipRect();
	}

	// ---------------- 3D 拾取：实体 ID 缓冲（异步） ----------------
	// 点击时把请求交给渲染器（EditorViewportChannel），下一次场景渲染时读回鼠标附近的实体 ID，
	// 过 2 ~ 3 帧拿到结果再改选中。等待期间再点一次，新请求直接覆盖旧的；晚到的旧结果对不上 id，丢掉
	// 「添加物体」菜单的落点：对视口中心做一次 3D 拾取（带世界坐标），放在中心处的网格表面上；
	// 没命中（天空）、3D 层关掉、超时时放在相机前方 15 米。和点选 / 拖放共用一个通道，同一时间只有一个请求在途
	struct PendingAddPlacement
	{
		bool submitted = false;		// 请求已提交、在等结果
		uint64_t requestId = 0;
		std::chrono::steady_clock::time_point submitTime;
	};
	PendingAddPlacement s_addPlacement;

	struct Pending3DPick
	{
		bool active = false;
		uint64_t requestId = 0;
		std::chrono::steady_clock::time_point submitTime;
		uint64_t selectionVersion = 0;		// 提交时的选中版本：等待期间选中被别处改了（层级面板等），结果就不再应用
		SelectMode mode = SelectMode::Replace;	// 结果怎么并进选中（按下时的 Ctrl / Shift）
		bool isBox = false;
		std::vector<Entity> extra;			// 框选：同步得到的图标 / 2D 实体，和 3D 结果一起应用
	};
	Pending3DPick s_pending3DPick;
	uint64_t s_nextPickRequestId = 1;
	// 渲染器正常每帧出图，几十毫秒内就有结果；超过这个时间按「没点中」处理
	constexpr auto kPickTimeout = std::chrono::milliseconds(500);
	// 点选时读鼠标周围 (2r+1)² 的像素，鼠标下没有物体时取最近的那个，细小物体也点得中
	constexpr int kPickRadius = 2;

	std::shared_ptr<EditorViewportChannel> GetViewportChannel(EditorApp* app)
	{
		auto* renderer3D = app ? app->Runtime().GetRenderer3D() : nullptr;
		auto renderer = renderer3D ? renderer3D->GetVulkanRenderer() : nullptr;
		return renderer ? renderer->GetEditorViewportChannel() : nullptr;
	}

	// 鼠标 -> 渲染目标像素（渲染目标和视口尺寸可能不一致，和 Pick2DAtMouse 一样按渲染目标尺寸换算）
	bool MouseToTargetPixel(EditorApp* app, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize,
		glm::ivec2& pixel, glm::uvec2& targetSize)
	{
		if (!app || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;
		targetSize = glm::uvec2(app->GetRenderTargetWidth(), app->GetRenderTargetHeight());
		if (targetSize.x == 0 || targetSize.y == 0)
			return false;
		const glm::vec2 uv((mouse.x - viewportPos.x) / viewportSize.x, (mouse.y - viewportPos.y) / viewportSize.y);
		if (uv.x < 0.0f || uv.y < 0.0f || uv.x >= 1.0f || uv.y >= 1.0f)
			return false;
		pixel = glm::clamp(glm::ivec2(glm::floor(uv * glm::vec2(targetSize))), glm::ivec2(0), glm::ivec2(targetSize) - glm::ivec2(1));
		return true;
	}

	// 提交拾取请求（点选 / 框选共用）：撤掉之前在等的点选和「添加物体」落点请求
	void SubmitPickRequest(const std::shared_ptr<EditorViewportChannel>& channel, EditorPickRequest request, SelectMode mode, std::vector<Entity> extra)
	{
		if (s_pending3DPick.active)
			channel->CancelPick(s_pending3DPick.requestId);
		// 点选优先：「添加物体」的落点请求还在等就撤掉，点选结果出来后它会重新提交
		if (s_addPlacement.submitted)
		{
			channel->CancelPick(s_addPlacement.requestId);
			s_addPlacement.submitted = false;
		}

		request.id = s_nextPickRequestId++;
		channel->SubmitPick(request);

		s_pending3DPick.active = true;
		s_pending3DPick.requestId = request.id;
		s_pending3DPick.submitTime = std::chrono::steady_clock::now();
		s_pending3DPick.selectionVersion = s_selectionVersion;
		s_pending3DPick.mode = mode;
		s_pending3DPick.isBox = request.isBox;
		s_pending3DPick.extra = std::move(extra);
	}

	// 提交点选请求。返回 false：渲染器不可用 / 鼠标不在视口里，调用方按「没点中」处理
	bool Submit3DPick(EditorApp* app, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize, SelectMode mode)
	{
		auto channel = GetViewportChannel(app);
		glm::ivec2 pixel;
		glm::uvec2 targetSize;
		if (!channel || !MouseToTargetPixel(app, mouse, viewportPos, viewportSize, pixel, targetSize))
			return false;

		EditorPickRequest request;
		request.targetSize = targetSize;
		request.focus = pixel;
		request.rectMin = pixel - glm::ivec2(kPickRadius);
		request.rectMax = pixel + glm::ivec2(kPickRadius);
		request.isBox = false;
		SubmitPickRequest(channel, request, mode, {});
		return true;
	}

	// 提交框选请求（屏幕矩形 [a, b]，任意两个对角）。extra：图标 / 2D 层同步框到的实体。
	// 返回 false：渲染器不可用 / 矩形整个在视口外，调用方直接用 extra
	bool Submit3DBoxPick(EditorApp* app, const ImVec2& a, const ImVec2& b, const ImVec2& viewportPos, const ImVec2& viewportSize,
		SelectMode mode, std::vector<Entity> extra)
	{
		auto channel = GetViewportChannel(app);
		const glm::uvec2 targetSize(app ? app->GetRenderTargetWidth() : 0u, app ? app->GetRenderTargetHeight() : 0u);
		if (!channel || targetSize.x == 0 || targetSize.y == 0 || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		auto toPixel = [&](const ImVec2& p)
			{
				const glm::vec2 uv((p.x - viewportPos.x) / viewportSize.x, (p.y - viewportPos.y) / viewportSize.y);
				return glm::ivec2(glm::floor(uv * glm::vec2(targetSize)));
			};
		const glm::ivec2 pa = toPixel(a);
		const glm::ivec2 pb = toPixel(b);
		const glm::ivec2 lo = glm::max(glm::min(pa, pb), glm::ivec2(0));
		const glm::ivec2 hi = glm::min(glm::max(pa, pb), glm::ivec2(targetSize) - glm::ivec2(1));
		if (lo.x > hi.x || lo.y > hi.y)
			return false;

		EditorPickRequest request;
		request.targetSize = targetSize;
		request.rectMin = lo;
		request.rectMax = hi;
		request.focus = (lo + hi) / 2;
		request.isBox = true;
		SubmitPickRequest(channel, request, mode, std::move(extra));
		return true;
	}

	// 不再等正在进行的请求（选中被别的途径改了、点到了 2D 物体等）
	void Cancel3DPick(EditorApp* app)
	{
		if (!s_pending3DPick.active)
			return;
		if (auto channel = GetViewportChannel(app))
			channel->CancelPick(s_pending3DPick.requestId);
		s_pending3DPick.active = false;
	}

	// 每帧调一次。有结论（拿到结果或超时）时返回 true，picked 为拾取到的实体：
	// 点选最多一个（没点中为空），框选是矩形里的全部（按实体 id 升序）
	bool Poll3DPick(EditorApp* app, std::vector<Entity>& picked)
	{
		picked.clear();
		if (!s_pending3DPick.active)
			return false;
		auto channel = GetViewportChannel(app);
		World* world = app ? app->GetWorld() : nullptr;
		if (!channel || !world)
		{
			s_pending3DPick.active = false;
			return true;
		}

		if (auto result = channel->PollPick(s_pending3DPick.requestId))
		{
			s_pending3DPick.active = false;
			if (!result->valid)
				return true;
			auto add = [&](uint32_t id)
				{
					if (id == UINT32_MAX)
						return;
					const Entity entity(world, static_cast<EntityID>(id));
					if (world->IsValidWorldEntity(entity))
						picked.push_back(entity);
				};
			if (s_pending3DPick.isBox)
			{
				for (uint32_t id : result->entities)
					add(id);
			}
			else
				add(result->focusEntity);
			return true;
		}

		if (std::chrono::steady_clock::now() - s_pending3DPick.submitTime > kPickTimeout)
		{
			channel->CancelPick(s_pending3DPick.requestId);
			s_pending3DPick.active = false;
			return true;
		}
		return false;
	}

	// 多选删除的确认框：每帧在所有窗口之外调用一次（DrawSceneView 末尾），打开和绘制在同一个 ID 栈里
	void DrawMultiDeleteConfirm(EditorApp* app)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (s_multiDelete.requested)
		{
			s_multiDelete.requested = false;
			if (world && !s_multiDelete.entities.empty())
				ImGui::OpenPopup(kMultiDeletePopup);
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (!ImGui::BeginPopupModal(kMultiDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		// 打开期间实体可能被别处销毁（脚本、切场景等）：只算还在的
		std::vector<Entity> alive;
		if (world)
		{
			for (const Entity& e : s_multiDelete.entities)
			{
				if (e && world->IsValidWorldEntity(e))
					alive.push_back(e);
			}
		}

		ImGui::Text("确定删除选中的 %d 个实体？", static_cast<int>(alive.size()));
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "编辑器没有撤销，删除后无法恢复。");
		ImGui::Separator();
		const size_t listed = std::min(alive.size(), kMultiDeleteListMax);
		for (size_t i = 0; i < listed; ++i)
		{
			const Entity& e = alive[i];
			if (const auto* tag = e.tryGetComponent<TagEditorName>(); tag && !tag->name.empty())
				ImGui::BulletText("%s", tag->name.c_str());
			else
				ImGui::BulletText("Unnamed_Entity_%d", static_cast<int>(e.getId()));
		}
		if (alive.size() > listed)
			ImGui::TextDisabled("……还有 %d 个", static_cast<int>(alive.size() - listed));
		ImGui::Spacing();

		bool close = false;
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.27f, 0.24f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.15f, 0.13f, 1.0f));
		const bool confirm = ImGui::Button("删除", ImVec2(120.0f, 0.0f));
		ImGui::PopStyleColor(3);
		ImGui::SameLine();
		// 默认焦点放在「取消」上：误按回车 / 空格不会删
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		if (ImGui::Button("取消", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			close = true;

		if (confirm && world && !alive.empty())
		{
			// 父子都选中时删父节点可能已经连带删掉子节点，逐个再检查一次
			world->SubmitCommand([entities = alive](World& w)
				{
					for (const Entity& e : entities)
					{
						if (w.IsValidWorldEntity(e))
							w.destroyEntity(e);
					}
				});
			Cancel3DPick(app);
			s_pickedBy2D = Entity();
			SetSelectionList({});
			close = true;
		}
		else if (confirm)
			close = true;

		if (close)
		{
			s_multiDelete.entities.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// ---------------- 视口点选 / 框选 ----------------
	// 左键在视口里按下（没点在手柄上、没被 DDGI 探针编辑用掉）后：松手时移动不到 kBoxSelectThreshold 像素按点选处理，
	// 否则按框选处理。点选和以前一样按 图标 → 2D → 3D（异步）的顺序；框选三层一起：
	//   图标看中心点在不在框里，2D 看精灵 / 动图的矩形和框有没有重叠，3D 看实体 ID 缓冲里框内出现过的网格（异步）
	struct BoxSelectState
	{
		bool armed = false;			// 左键按下了，在等松手
		bool dragging = false;		// 已经拖过阈值，按框选处理
		ImVec2 start{ 0.0f, 0.0f };
		SelectMode mode = SelectMode::Replace;
	};
	BoxSelectState s_boxSelect;
	constexpr float kBoxSelectThreshold = 4.0f;

	// 上一帧视口的屏幕矩形：鼠标在里面时 Scene View 窗口不响应左键拖动移动窗口（框选要用左键拖动）
	struct LastViewportRect
	{
		bool valid = false;
		ImVec2 min{ 0.0f, 0.0f };
		ImVec2 max{ 0.0f, 0.0f };
	};
	LastViewportRect s_lastViewportRect;

	void ClickSelect(EditorApp* app, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize, SelectMode mode)
	{
		const auto visibility = app->GetLayerVisibility();
		bool pickedBy2D = false;
		// 图标只在 3D 层打开时画（ViewportIcons::Draw），没画时 Pick 什么都点不中
		Entity picked = ViewportIcons::Pick(app, mouse);
		if (!picked && visibility.layer2D)
		{
			picked = Pick2DAtMouse(app, mouse, viewportPos, viewportSize);
			pickedBy2D = static_cast<bool>(picked);
		}
		if (mode == SelectMode::Replace)
			s_pickedBy2D = pickedBy2D ? picked : Entity();
		if (picked)
		{
			Cancel3DPick(app);
			ApplySelection({ picked }, mode);
		}
		// 3D：读实体 ID 缓冲，异步。结果到了（几帧后）在 DrawSceneView 开头的 Poll3DPick 里改选中
		else if (!visibility.layer3D || !Submit3DPick(app, mouse, viewportPos, viewportSize, mode))
		{
			Cancel3DPick(app);
			if (mode == SelectMode::Replace)
				ApplySelection({}, SelectMode::Replace);		// 点在空白处：取消选中（Ctrl / Shift 时保持不变）
		}
	}

	// 框选里的 2D 实体：在场景线程上把每个精灵 / 动图的矩形映射到视口屏幕坐标再和框比（2D 相机旋转 / 缩放都不影响）
	std::vector<Entity> Pick2DRectAtScreen(EditorApp* app, const ImVec2& a, const ImVec2& b, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return {};
		const glm::vec2 lo(std::min(a.x, b.x), std::min(a.y, b.y));
		const glm::vec2 hi(std::max(a.x, b.x), std::max(a.y, b.y));
		std::vector<Entity> result;
		world->SubmitCommand([&](World& w)
			{
				const auto view = Viewport2DView(RenderSystem::GetActiveCamera2D(w), app, viewportSize);
				const glm::vec2 target = glm::max(view.targetSize, glm::vec2(1.0f));
				const auto toViewport = [&](const glm::vec2& p)
					{
						const glm::vec2 pixel = view.WorldToScreen(p);
						return glm::vec2(viewportPos.x + pixel.x / target.x * viewportSize.x, viewportPos.y + pixel.y / target.y * viewportSize.y);
					};
				result = RenderSystem::Pick2DRect(w, toViewport, lo, hi);
			}).get();
		return result;
	}

	void BoxSelect(EditorApp* app, const ImVec2& a, const ImVec2& b, const ImVec2& viewportPos, const ImVec2& viewportSize, SelectMode mode)
	{
		const auto visibility = app->GetLayerVisibility();
		std::vector<Entity> extra;
		if (visibility.layer3D)
			extra = ViewportIcons::PickRect(app, a, b);
		if (visibility.layer2D)
		{
			auto picked2D = Pick2DRectAtScreen(app, a, b, viewportPos, viewportSize);
			extra.insert(extra.end(), picked2D.begin(), picked2D.end());
		}
		if (mode == SelectMode::Replace)
			s_pickedBy2D = Entity();
		// 3D 结果几帧后才到，到了和 extra 一起应用；3D 层关掉 / 渲染器不可用时直接用 extra
		if (!visibility.layer3D || !Submit3DBoxPick(app, a, b, viewportPos, viewportSize, mode, extra))
		{
			Cancel3DPick(app);
			ApplySelection(extra, mode);
		}
	}

	// 每帧在 Scene View 里调一次（视口图标 Draw 之后：点选图标要用本帧的屏幕矩形）。按下后鼠标移出视口也照常跟踪
	void UpdateBoxSelect(EditorApp* app, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		if (!s_boxSelect.armed)
			return;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGuizmo::IsUsing())
		{
			s_boxSelect.armed = false;
			return;
		}

		const ImVec2 vpMax(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
		ImVec2 mouse = ImGui::GetMousePos();
		mouse.x = std::clamp(mouse.x, viewportPos.x, vpMax.x);
		mouse.y = std::clamp(mouse.y, viewportPos.y, vpMax.y);
		const float dx = mouse.x - s_boxSelect.start.x;
		const float dy = mouse.y - s_boxSelect.start.y;
		if (!s_boxSelect.dragging && dx * dx + dy * dy > kBoxSelectThreshold * kBoxSelectThreshold)
			s_boxSelect.dragging = true;

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (s_boxSelect.dragging)
			{
				const ImVec2 lo(std::min(s_boxSelect.start.x, mouse.x), std::min(s_boxSelect.start.y, mouse.y));
				const ImVec2 hi(std::max(s_boxSelect.start.x, mouse.x), std::max(s_boxSelect.start.y, mouse.y));
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->PushClipRect(viewportPos, vpMax, true);
				drawList->AddRectFilled(lo, hi, IM_COL32(70, 200, 255, 40));
				drawList->AddRect(lo, hi, IM_COL32(70, 200, 255, 220), 0.0f, 0, 1.0f);
				drawList->PopClipRect();
			}
			return;
		}

		// 松手
		s_boxSelect.armed = false;
		if (s_boxSelect.dragging)
			BoxSelect(app, s_boxSelect.start, mouse, viewportPos, viewportSize, s_boxSelect.mode);
		else
			ClickSelect(app, s_boxSelect.start, viewportPos, viewportSize, s_boxSelect.mode);
	}

	// Delete 键：在当前窗口（层级面板 / 场景视口）获得焦点时调用。单选直接删除，多选弹确认框（同右键菜单）。
	// 正在输入文字（重命名等）、有弹窗或菜单打开、正在拖手柄或框选时不响应
	void HandleDeleteShortcut(EditorApp* app)
	{
		if (!app || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput || ImGuizmo::IsUsing() || s_boxSelect.armed
			|| ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
			return;
		if (!ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			return;

		World* world = app->GetWorld();
		SyncSelection(world);
		if (!world || s_selection.empty())
			return;
		if (s_selection.size() > 1)
		{
			RequestMultiDelete();
			return;
		}
		world->SubmitCommand([entity = s_selection.front()](World& w)
			{
				if (w.IsValidWorldEntity(entity))
					w.destroyEntity(entity);
			});
		Cancel3DPick(app);
		s_pickedBy2D = Entity();
		SetSelectionList({});
	}

	// ---------------- 拖放：拖动过程中持续拾取 ----------------
	// 资源 / 文件拖在场景视口上方时，对鼠标位置不停地做 3D 拾取（一次只有一个请求在途），记下最近一次的结果：
	// 松手时直接用，不用等几帧；要挂到物体上的资源（脚本、UI、材质）还会把鼠标下的网格当成选中描边高亮出来。
	// 和点选共用一个通道：点选的请求还在等时不提交，免得把它挤掉
	enum class DropKind
	{
		None,
		Surface,		// 摆放物体（模型）：要鼠标下的表面位置
		Entity			// 挂到物体上（脚本、UI、材质）：要鼠标下的实体
	};

	struct DragHoverPick
	{
		bool hovering = false;			// 有资源拖在视口上方
		DropKind kind = DropKind::None;
		bool pending = false;			// 有请求在途
		uint64_t requestId = 0;
		std::chrono::steady_clock::time_point submitTime;
		// 最近一次 3D 结果
		bool hasResult = false;
		uint32_t entity = UINT32_MAX;
		bool hasSurface = false;
		glm::vec3 surface{ 0.0f };
		// 鼠标在视口图标上（图标优先于 3D，也不需要描边高亮：图标自己有悬停高亮）
		bool overIcon = false;
	};
	DragHoverPick s_dragHover;

	DropKind ClassifyDragPayload(const ImGuiPayload* payload, ProjectManager* projectManager)
	{
		if (!payload || !payload->Data || payload->DataSize <= 0)
			return DropKind::None;
		const char* data = static_cast<const char*>(payload->Data);
		if (payload->IsDataType("ASSET_GUID"))
		{
			if (!projectManager || strlen(data) == 0)
				return DropKind::None;
			auto meta = projectManager->GetDatabase()->GetAssetMetaByGuid(AssetGUID(data));
			if (!meta)
				return DropKind::None;
			switch (meta->type)
			{
			case AssetType::StaticMesh:
			case AssetType::SkinnedMesh:
			case AssetType::StaticModel:
			case AssetType::SkinnedModel:
				return DropKind::Surface;
			case AssetType::Material:
			case AssetType::Script:
			case AssetType::UiDocument:
				return DropKind::Entity;
			default:
				return DropKind::None;
			}
		}
		if (payload->IsDataType("ASSET_FILE"))
		{
			const std::string ext = fs::path(std::string(data)).extension().string();
			return (ext == ".lua" || ext == ".ui") ? DropKind::Entity : DropKind::None;
		}
		return DropKind::None;
	}

	void EndDragHover(EditorApp* app)
	{
		if (s_dragHover.pending)
		{
			if (auto channel = GetViewportChannel(app))
				channel->CancelPick(s_dragHover.requestId);
		}
		s_dragHover = DragHoverPick{};
	}

	// 拖在视口上方的每一帧调一次：取上一个请求的结果，再按当前鼠标位置提交下一个
	void UpdateDragHover(EditorApp* app, DropKind kind, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		s_dragHover.hovering = true;
		s_dragHover.kind = kind;
		// 图标用上一帧画出来的屏幕矩形（这段在图标绘制之前），不经过场景线程，每帧调也没有开销
		s_dragHover.overIcon = static_cast<bool>(ViewportIcons::Pick(app, mouse));

		auto channel = GetViewportChannel(app);
		if (!channel || !app->GetLayerVisibility().layer3D)
		{
			if (s_dragHover.pending && channel)
				channel->CancelPick(s_dragHover.requestId);
			s_dragHover.pending = false;
			s_dragHover.hasResult = false;
			return;
		}

		if (s_dragHover.pending)
		{
			if (auto result = channel->PollPick(s_dragHover.requestId))
			{
				s_dragHover.pending = false;
				s_dragHover.hasResult = result->valid;
				s_dragHover.entity = result->valid ? result->focusEntity : UINT32_MAX;
				s_dragHover.hasSurface = result->valid && result->hasPosition;
				s_dragHover.surface = result->focusPosition;
			}
			else if (std::chrono::steady_clock::now() - s_dragHover.submitTime > kPickTimeout)
			{
				channel->CancelPick(s_dragHover.requestId);
				s_dragHover.pending = false;
			}
		}

		if (s_dragHover.pending || s_pending3DPick.active || s_addPlacement.submitted)
			return;

		glm::ivec2 pixel;
		glm::uvec2 targetSize;
		if (!MouseToTargetPixel(app, mouse, viewportPos, viewportSize, pixel, targetSize))
			return;

		EditorPickRequest request;
		request.id = s_nextPickRequestId++;
		request.targetSize = targetSize;
		request.focus = pixel;
		request.rectMin = pixel - glm::ivec2(kPickRadius);
		request.rectMax = pixel + glm::ivec2(kPickRadius);
		request.wantPosition = true;
		channel->SubmitPick(request);

		s_dragHover.pending = true;
		s_dragHover.requestId = request.id;
		s_dragHover.submitTime = std::chrono::steady_clock::now();
	}

	// 选中描边顺带高亮的拖放目标：要挂到物体上的资源拖在一个网格上方时，就是那个网格
	Entity DragHighlightEntity(EditorApp* app)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || !s_dragHover.hovering || s_dragHover.kind != DropKind::Entity || s_dragHover.overIcon
			|| !s_dragHover.hasResult || s_dragHover.entity == UINT32_MAX)
			return Entity();
		const Entity entity(world, static_cast<EntityID>(s_dragHover.entity));
		return world->IsValidWorldEntity(entity) ? entity : Entity();
	}

	// 松手时的目标：和点选同样的顺序（视口图标 → 2D → 3D）。3D 用拖动过程中最近一次的拾取结果，
	// 2D 只在这里同步拾取一次（要等场景线程，不适合每帧做）
	ViewportDropTarget ResolveDropTarget(EditorApp* app, const ImVec2& mouse, const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		ViewportDropTarget target;
		World* world = app ? app->GetWorld() : nullptr;
		if (!world)
			return target;

		glm::mat4 view(1.f), projection(1.f);
		glm::vec3 cameraPosition(0.f), cameraDirection(0.f, 0.f, -1.f);
		if (app->GetCameraSnapshot(view, projection, cameraPosition, cameraDirection))
		{
			glm::mat4 projOG = projection;
			projOG[1][1] *= -1;
			ImGuizmo::ComputeMouseRay((float*)&view, (float*)&projOG, mouse, viewportPos, viewportSize,
				(float*)&target.rayOrigin, (float*)&target.rayDirection);
		}
		else
		{
			target.rayOrigin = cameraPosition;
			target.rayDirection = cameraDirection;
		}

		const auto visibility = app->GetLayerVisibility();
		Entity entity = ViewportIcons::Pick(app, mouse);
		if (!entity && visibility.layer2D)
			entity = Pick2DAtMouse(app, mouse, viewportPos, viewportSize);
		if (!entity && visibility.layer3D && s_dragHover.hasResult && s_dragHover.entity != UINT32_MAX)
		{
			const Entity picked(world, static_cast<EntityID>(s_dragHover.entity));
			if (world->IsValidWorldEntity(picked))
				entity = picked;
		}
		target.entity = entity;

		if (visibility.layer3D && s_dragHover.hasResult && s_dragHover.hasSurface)
		{
			target.hasSurface = true;
			target.surfacePosition = s_dragHover.surface;
		}
		return target;
	}

	// 「添加物体」的落点。返回 true 表示有结果（position 已填好），false 表示还在等拾取结果（下一帧再调）
	bool ResolveAddPlacement(EditorApp* app, const ImVec2& viewportPos, const ImVec2& viewportSize, glm::vec3& position)
	{
		glm::mat4 view(1.f), projection(1.f);
		glm::vec3 cameraPosition(0.f), cameraDirection(0.f, 0.f, -1.f);
		const bool hasCamera = app && app->GetCameraSnapshot(view, projection, cameraPosition, cameraDirection);
		const glm::vec3 fallback = cameraPosition + cameraDirection * 15.f;

		auto channel = GetViewportChannel(app);
		auto finish = [&](const glm::vec3& p) {
			position = p;
			s_addPlacement = PendingAddPlacement{};
			return true;
			};
		if (!hasCamera || !channel || !app->GetLayerVisibility().layer3D)
		{
			if (s_addPlacement.submitted && channel)
				channel->CancelPick(s_addPlacement.requestId);
			return finish(fallback);
		}

		if (!s_addPlacement.submitted)
		{
			// 点选 / 拖放的请求还在等：等它们出结果再提交，免得把它们挤掉
			if (s_pending3DPick.active || s_dragHover.pending)
				return false;

			glm::ivec2 pixel;
			glm::uvec2 targetSize;
			const ImVec2 center(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y * 0.5f);
			if (!MouseToTargetPixel(app, center, viewportPos, viewportSize, pixel, targetSize))
				return finish(fallback);

			EditorPickRequest request;
			request.id = s_nextPickRequestId++;
			request.targetSize = targetSize;
			request.focus = pixel;
			request.rectMin = pixel;		// 只看正中心那一个像素：旁边的小物体不该抢走落点
			request.rectMax = pixel;
			request.wantPosition = true;
			channel->SubmitPick(request);

			s_addPlacement.submitted = true;
			s_addPlacement.requestId = request.id;
			s_addPlacement.submitTime = std::chrono::steady_clock::now();
			return false;
		}

		if (auto result = channel->PollPick(s_addPlacement.requestId))
			return finish((result->valid && result->hasPosition) ? result->focusPosition : fallback);

		if (std::chrono::steady_clock::now() - s_addPlacement.submitTime > kPickTimeout)
		{
			channel->CancelPick(s_addPlacement.requestId);
			return finish(fallback);
		}
		return false;
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
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::Checkbox("图标", &ViewportIcons::Visible());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("显示灯光、相机、DDGI 体积、特效、音源、空实体的视口图标（关掉时也不能点选它们；3D 层关掉时不显示）");
		ImGui::SameLine();
		ImGui::Checkbox("包围盒", &s_showSelectionBounds);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("选中物体的3D/2D包围盒（青色）");
		ImGui::SameLine(0.0f, 16.0f);
		PhysicsDebugView::DrawToolbar();
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
	SyncSelection(editorApp->GetWorld());
	ImGui::Begin("Scene Objects");
	HandleDeleteShortcut(editorApp);
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
		bool isSelected = IsSelected(item.entity);
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

			// 处理点击选中：Ctrl 切换、Shift 从上一次点的那一行选到这一行（加入）、否则只选这一个
			if (ImGui::IsItemClicked())
			{
				const SelectMode mode = SelectModeFromKeys();
				if (mode == SelectMode::Add && s_hierarchyRangeAnchor && s_hierarchyRangeAnchor != item.entity)
				{
					const auto anchorIt = std::find_if(items.begin(), items.end(), [](const auto& other) { return other.entity == s_hierarchyRangeAnchor; });
					if (anchorIt != items.end())
					{
						const size_t from = static_cast<size_t>(anchorIt - items.begin());
						const size_t to = static_cast<size_t>(&item - items.data());
						std::vector<Entity> range;
						for (size_t k = std::min(from, to); k <= std::max(from, to); ++k)
							range.push_back(items[k].entity);
						// 区间里点的那一行之外的按列表顺序；手柄和面板跟着原来的第一个（没有就是区间起点）
						ApplySelection(range, SelectMode::Add);
					}
					else
						ApplySelection({ item.entity }, SelectMode::Add);
				}
				else
				{
					ApplySelection({ item.entity }, mode);
					s_hierarchyRangeAnchor = item.entity;
				}
			}

			// 处理右键菜单：点在没选中的行上先只选它；点在选中的行上保留多选
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				if (!IsSelected(item.entity))
				{
					ApplySelection({ item.entity }, SelectMode::Replace);
					s_hierarchyRangeAnchor = item.entity;
				}
				ImGui::OpenPopup("EntityContextMenu");
			}

			if (isSelected && s_selection.size() <= 1 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
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
				// 多选：删除全部选中（先弹确认框）；复制不可用
				const bool multi = s_selection.size() > 1;
				if (multi)
				{
					if (ImGui::MenuItem(std::format("Delete Selected ({})...", s_selection.size()).c_str()))
						RequestMultiDelete();
				}
				else if (ImGui::MenuItem("Delete"))
				{
					editorApp->GetWorld()->SubmitCommand([entity = item.entity, world = editorApp->GetWorld()]() -> void {
						if (!entity || !world)
							return;
						world->destroyEntity(entity);
						}
					);
				}
				if (ImGui::MenuItem("Duplicate", nullptr, false, !multi))
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
	SyncSelection(world);
	// 后台凸分解完成时把结果写回 Physics 组件（属性面板没显示这个实体时也要写回）
	PhysicsShapeFromModel::Decomposition::Update(world);
	const bool entitySelected = world && selectedEntity && world->IsValidWorldEntity(selectedEntity);
	if (entitySelected && s_selection.size() > 1)
	{
		// 多选：只读显示手柄所在实体（第一个有对应组件的）的 Transform / Transform2D，改动只能用手柄一起调
		const GizmoTargets targets = ResolveGizmoTargets();
		ImGui::Text("已选择 %d 个实体", static_cast<int>(s_selection.size()));
		if (targets.anchor)
		{
			ImGui::Text("%s：%d（%d 个实体有%s）", targets.is3D ? "Transform 显示" : "Transform2D 显示",
				static_cast<int>(targets.anchor.getId()), static_cast<int>(targets.entities.size()), targets.is3D ? " Transform" : " Transform2D");
			ImGui::TextDisabled("多选时属性只读，用手柄一起调整；没有对应组件的实体不受影响");
		}
		else
			ImGui::TextDisabled("选中的实体都没有 Transform / Transform2D，无法调整");
		ImGui::Separator();
		if (targets.anchor)
		{
			const IComponent* shown = targets.is3D
				? static_cast<const IComponent*>(targets.anchor.tryGetComponent<Transform>())
				: static_cast<const IComponent*>(targets.anchor.tryGetComponent<Transform2D>());
			auto components = PropertiesHelper::GetAllComponents(targets.anchor);
			std::erase_if(components, [shown](const ComponentData& data) { return !shown || data.component != shown; });
			ImGui::BeginDisabled();
			PropertiesHelper::DrawAllProperties(targets.anchor, components, projectManager);
			ImGui::EndDisabled();
		}
	}
	else if (entitySelected)
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
	// 默认够宽，直接是左预览、右参数的两栏
	ImGui::SetNextWindowSize(ImVec2(940.0f, 640.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(std::format("Material Editor: {}###MaterialEditor", meta->name).c_str(), &open, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
		s_materialEditorOpen = open;
		if (!open)
		{
			restoreOrigin();
			s_materialOrigin.valid = false;
			MaterialPreview::Reset();
		}
		return;
	}

	bool changed = false;
	auto properties = materialAsset->GetProperties();
	const auto& dependencies = materialAsset->GetTextureDependencies();
	std::optional<TextureType> clearType;		// 遍历时引用着 dependencies，清除放到最后

	auto textureGuidOf = [&](TextureType type) -> AssetGUID
		{
			auto it = dependencies.find(type);
			return it != dependencies.end() ? it->second : AssetGUID{};
		};
	auto hasTexture = [&](TextureType type) { return dependencies.find(type) != dependencies.end(); };

	// ---- 拖入贴图：贴图直接填进槽；精灵先用它的原图生成一张贴图，导入成功后再填 ----
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

	// ---- 参数行：[贴图槽] 名称 [数值 / 贴图名] [清除]，贴图和它影响的数值放在同一行 ----
	constexpr float kSlotSide = 32.0f;
	const float frameHeight = ImGui::GetFrameHeight();
	const float labelWidth = ImGui::CalcTextSize("Metallic Roughness").x + ImGui::GetStyle().ItemSpacing.x;
	using OptionalSlot = std::optional<TextureType>;

	auto beginSection = [&](const char* title) -> bool
		{
			if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
				return false;
			if (!ImGui::BeginTable(title, 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
				return false;
			ImGui::TableSetupColumn("slot", ImGuiTableColumnFlags_WidthFixed, kSlotSide);
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("clear", ImGuiTableColumnFlags_WidthFixed, frameHeight);
			return true;
		};
	auto endSection = []() { ImGui::EndTable(); };

	// 进入数值列；调用方接着画控件（宽度已经设成占满）
	auto beginRow = [&](const char* label, OptionalSlot slot)
		{
			ImGui::PushID(label);
			const float rowHeight = slot ? kSlotSide : frameHeight;
			const float offsetY = (rowHeight - frameHeight) * 0.5f;
			ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

			ImGui::TableSetColumnIndex(0);
			if (slot)
			{
				AssetThumbnails::DrawThumbnailBox(projectManager, textureGuidOf(*slot), kSlotSide);
				acceptTextureDrop(*slot);
				if (!hasTexture(*slot) && ImGui::IsItemHovered())
					ImGui::SetTooltip("把贴图（或精灵，会先生成贴图）拖到这里");
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			if (slot)
				acceptTextureDrop(*slot);

			ImGui::TableSetColumnIndex(2);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
			ImGui::SetNextItemWidth(-FLT_MIN);
		};
	auto endRow = [&](OptionalSlot slot)
		{
			ImGui::TableSetColumnIndex(3);
			if (slot && hasTexture(*slot))
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (kSlotSide - frameHeight) * 0.5f);
				if (ImGui::Button("×", ImVec2(frameHeight, frameHeight)))
					clearType = *slot;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("清除贴图");
			}
			ImGui::PopID();
		};

	// 数值被贴图取代时（着色器里贴图优先）控件变灰，悬停说明原因
	auto overridden = [](bool disabled, const char* reason, auto&& draw) -> bool
		{
			if (disabled)
				ImGui::BeginDisabled();
			const bool edited = draw();
			if (disabled)
			{
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", reason);
			}
			return edited;
		};

	// 只有贴图、没有数值的行：显示贴图名
	auto textureNameCell = [&](TextureType type, const char* hint)
		{
			const AssetGUID guid = textureGuidOf(type);
			auto textureMeta = guid.empty() ? nullptr : database->GetAssetMetaByGuid(guid);
			ImGui::AlignTextToFramePadding();
			if (guid.empty())
				ImGui::TextDisabled("%s", hint);
			else if (textureMeta)
				ImGui::TextUnformatted(textureMeta->name.c_str());
			else
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "资产不存在");
			acceptTextureDrop(type);
			if (ImGui::IsItemHovered())
			{
				if (textureMeta)
					ImGui::SetTooltip("%s\n%s", textureMeta->name.c_str(), textureMeta->path.c_str());
				else if (!guid.empty())
					ImGui::SetTooltip("资产不存在：%s", guid.c_str());
			}
		};

	auto drawSections = [&]()
		{
			const bool metallicRoughnessTex = hasTexture(TextureType::MetallicRoughness);

			if (beginSection("Surface"))
			{
				beginRow("Albedo", TextureType::Albedo);
				changed |= ImGui::ColorEdit3("##Albedo", glm::value_ptr(properties.albedo));
				if (hasTexture(TextureType::Albedo) && ImGui::IsItemHovered())
					ImGui::SetTooltip("和 Albedo 贴图相乘");
				endRow(TextureType::Albedo);

				beginRow("Metallic", TextureType::Metallic);
				changed |= overridden(metallicRoughnessTex || hasTexture(TextureType::Metallic), "由贴图提供（Metallic / Metallic Roughness 贴图的 B 通道），数值不生效",
					[&] { return ImGui::DragFloat("##Metallic", &properties.metallic, 0.01f, 0.f, 1.f); });
				endRow(TextureType::Metallic);

				beginRow("Roughness", TextureType::Roughness);
				changed |= overridden(metallicRoughnessTex || hasTexture(TextureType::Roughness), "由贴图提供（Roughness / Metallic Roughness 贴图的 G 通道），数值不生效",
					[&] { return ImGui::DragFloat("##Roughness", &properties.roughness, 0.01f, 0.001f, 1.f); });
				endRow(TextureType::Roughness);

				beginRow("Metallic Roughness", TextureType::MetallicRoughness);
				textureNameCell(TextureType::MetallicRoughness, "G = 粗糙度，B = 金属度（优先于上面两张）");
				endRow(TextureType::MetallicRoughness);

				beginRow("Normal", TextureType::Normal);
				textureNameCell(TextureType::Normal, "拖入法线贴图");
				endRow(TextureType::Normal);

				beginRow("Ambient Occlusion", TextureType::AO);
				changed |= overridden(hasTexture(TextureType::AO), "由 AO 贴图（R 通道）提供，数值不生效",
					[&] { return ImGui::DragFloat("##AO", &properties.ambientOcclusion, 0.01f, 0.f, 1.f); });
				endRow(TextureType::AO);

				beginRow("Height", TextureType::Height);
				textureNameCell(TextureType::Height, "当前渲染没有用到");
				endRow(TextureType::Height);
				endSection();
			}

			if (beginSection("Emission"))
			{
				beginRow("Emission Color", TextureType::Emissive);
				changed |= overridden(hasTexture(TextureType::Emissive), "由自发光贴图提供，颜色不生效（强度仍然生效）",
					[&] { return ImGui::ColorEdit3("##EmissionColor", glm::value_ptr(properties.emissionColor)); });
				endRow(TextureType::Emissive);

				beginRow("Emission Strength", std::nullopt);
				changed |= ImGui::DragFloat("##EmissionStrength", &properties.emissionStrength, 0.01f, 0.f, 100.f);
				endRow(std::nullopt);
				endSection();
			}

			if (beginSection("Transparency"))
			{
				int alphaMode = static_cast<int>(properties.alphamode);
				const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
				beginRow("Alpha Mode", std::nullopt);
				if (ImGui::Combo("##AlphaMode", &alphaMode, alphaModes, IM_ARRAYSIZE(alphaModes)))
				{
					properties.alphamode = static_cast<AlphaMode>(alphaMode);
					changed = true;
				}
				endRow(std::nullopt);

				beginRow("Opacity", TextureType::Opacity);
				changed |= ImGui::DragFloat("##Opacity", &properties.opacity, 0.01f, 0.f, 1.f);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("和 Opacity 贴图（R 通道）相乘；没有 Opacity 贴图时取 Albedo 贴图的 alpha");
				endRow(TextureType::Opacity);

				if (properties.alphamode == AlphaMode::Mask)
				{
					beginRow("Mask Threshold", std::nullopt);
					changed |= ImGui::DragFloat("##MaskThreshold", &properties.maskthreshold, 0.01f, 0.f, 1.f);
					endRow(std::nullopt);
				}

				beginRow("IOR", std::nullopt);
				changed |= ImGui::DragFloat("##IOR", &properties.IOR, 0.01f, 1.f, 3.f);
				endRow(std::nullopt);
				endSection();
			}

			if (beginSection("Rendering"))
			{
				beginRow("Two Sided", std::nullopt);
				changed |= ImGui::Checkbox("##TwoSided", &properties.twosided);
				endRow(std::nullopt);
				endSection();
			}
		};

	// ---- 实时预览：运行时材质的当前状态（含未保存的修改），有变化才重画 ----
	// fitHeight：左栏里按栏宽和剩余高度取最大的正方形（下面留工具条的高度）；单栏时限制大小
	auto drawPreview = [&](bool fitHeight)
		{
			const float toolbarHeight = ImGui::GetFrameHeightWithSpacing() * 6.0f + ImGui::GetStyle().ItemSpacing.y;
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			float side = fitHeight ? std::min(avail.x, avail.y - toolbarHeight) : std::min(avail.x, 320.0f);
			side = std::floor(std::clamp(side, 128.0f, 512.0f));
			if (avail.x > side)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail.x - side) * 0.5f));
			// AsMaterial：第一次调用时建运行时材质（等依赖贴图加载完），之后直接返回；属性 / 贴图槽的修改会同步到它上面
			MaterialPreview::Draw(meta->guid, materialAsset->AsMaterial(), side);
		};

	// ---- 布局：宽窗口左预览、右参数（中间可以拖动分隔），两边各自滚动；窄窗口上下排成一栏 ----
	const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
	ImGui::BeginChild("MaterialEditorContent", ImVec2(0.f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (ImGui::GetContentRegionAvail().x >= 620.0f)
	{
		if (ImGui::BeginTable("MaterialEditorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.42f);
			ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::BeginChild("##PreviewPane", ImVec2(0.f, 0.f));
			drawPreview(true);
			ImGui::EndChild();
			ImGui::TableSetColumnIndex(1);
			ImGui::BeginChild("##PropertyPane", ImVec2(0.f, 0.f));
			drawSections();
			ImGui::EndChild();
			ImGui::EndTable();
		}
	}
	else
	{
		ImGui::BeginChild("##SinglePane", ImVec2(0.f, 0.f));
		drawPreview(false);
		ImGui::Spacing();
		drawSections();
		ImGui::EndChild();
	}
	ImGui::EndChild();

	if (changed)
		materialAsset->SetProperties(properties);
	if (clearType)
		materialAsset->SetTextureDependency(*clearType, {}, nullptr);

	// ---- 底栏：保存 / 还原 + 未保存提示，右边是资产路径 ----
	static std::string s_materialSaveError;
	auto sameProperties = [](const MaterialProperties& a, const MaterialProperties& b)
		{
			return a.albedo == b.albedo && a.metallic == b.metallic && a.roughness == b.roughness
				&& a.ambientOcclusion == b.ambientOcclusion && a.opacity == b.opacity && a.IOR == b.IOR
				&& a.emissionColor == b.emissionColor && a.emissionStrength == b.emissionStrength
				&& a.alphamode == b.alphamode && a.maskthreshold == b.maskthreshold && a.twosided == b.twosided;
		};
	const bool dirty = s_materialOrigin.valid && s_materialOrigin.guid == meta->guid
		&& (!sameProperties(materialAsset->GetProperties(), s_materialOrigin.properties)
			|| materialAsset->GetTextureDependencies() != s_materialOrigin.textures);

	ImGui::Separator();
	ImGui::BeginChild("MaterialEditorFooter", ImVec2(0.f, footerHeight), ImGuiChildFlags_None);
	if (open)
	{
		if (!dirty)
			ImGui::BeginDisabled();
		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool restore = ImGui::Button("Restore");
		if (!dirty)
			ImGui::EndDisabled();

		if (save)
		{
			if (!projectManager->SaveAsset(meta->guid))
			{
				s_materialSaveError = "材质保存失败";
			}
			else
			{
				s_materialSaveError.clear();
				s_materialOrigin.properties = materialAsset->GetProperties();
				s_materialOrigin.textures = materialAsset->GetTextureDependencies();
				// 资产浏览器里的材质球按保存后的样子重画（缩略图只认磁盘上的元文件）
				AssetThumbnails::Invalidate(meta->guid);
			}
		}
		if (restore)
			restoreOrigin();

		ImGui::SameLine();
		if (!s_materialSaveError.empty())
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "%s", s_materialSaveError.c_str());
		else if (dirty)
			ImGui::TextColored(ImVec4(1.f, 0.78f, 0.3f, 1.f), "● 有未保存的修改");
		else
			ImGui::TextDisabled("已保存");

		// 路径靠右；放不下就跟在后面
		const float pathWidth = ImGui::CalcTextSize(meta->path.c_str()).x;
		ImGui::SameLine();
		const float spare = ImGui::GetContentRegionAvail().x - pathWidth;
		if (spare > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spare);
		ImGui::TextDisabled("%s", meta->path.c_str());
	}
	ImGui::EndChild();
	ImGui::End();
	s_materialEditorOpen = open;
	if (!open)
	{
		restoreOrigin();
		s_materialOrigin.valid = false;
		MaterialPreview::Reset();
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
	// 鼠标在视口画面上（上一帧的矩形）或正在框选时不让左键拖动移动窗口，标题栏 / 标签照常能拖
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool overViewport = s_lastViewportRect.valid
			&& mouse.x >= s_lastViewportRect.min.x && mouse.y >= s_lastViewportRect.min.y
			&& mouse.x < s_lastViewportRect.max.x && mouse.y < s_lastViewportRect.max.y;
		ImGui::Begin("Scene View", nullptr, (overViewport || s_boxSelect.armed) ? ImGuiWindowFlags_NoMove : ImGuiWindowFlags_None);
	}
	HandleDeleteShortcut(editorApp);

	ImGuizmo::BeginFrame();
	ImGuizmo::SetDrawlist();

	// 获取场景视口区域
	ImVec2 viewportPos = ImGui::GetCursorScreenPos();
	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	s_lastViewportRect = { true, viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y) };
	// 引擎 UI（ImGui 拾取）要知道渲染目标摆在窗口的哪块矩形里。
	if (auto inputContext = editorApp->GetInputContext())
		inputContext->SetViewportRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
	ImGuizmo::SetRect(viewportPos.x, viewportPos.y,
		viewportSize.x, viewportSize.y);

	// 吸收别处对 selectedEntity 的直接赋值（删除、复制、切场景等），去掉已销毁的实体
	SyncSelection(editorApp->GetWorld());

	// 3D 拾取（点选 / 框选）的异步结果：提交后选中没被别处改过才应用（手柄、属性面板等本帧就能看到新选中）
	if (std::vector<Entity> picked3D; Poll3DPick(editorApp, picked3D))
	{
		if (s_selectionVersion == s_pending3DPick.selectionVersion)
		{
			std::vector<Entity> picked = std::move(s_pending3DPick.extra);
			picked.insert(picked.end(), picked3D.begin(), picked3D.end());
			if (!s_pending3DPick.isBox && picked.size() == 1 && s_pending3DPick.mode == SelectMode::Replace)
				s_pickedBy2D = Entity();		// 点到的是 3D 网格
			ApplySelection(picked, s_pending3DPick.mode);
		}
		s_pending3DPick.extra.clear();
	}
	else if (s_pending3DPick.active && s_selectionVersion != s_pending3DPick.selectionVersion)
		Cancel3DPick(editorApp);

	// 选中描边：把当前选中推给渲染器（内容没变时什么都不做）。本帧稍后点击改的选中，下一帧推送
	if (auto channel = GetViewportChannel(editorApp))
	{
		World* world = editorApp->GetWorld();
		// 主选中（主色描边）：手柄所在的实体；都没有变换组件时用列表第一个
		const Entity anchor = ResolveGizmoTargets().anchor;
		const Entity primary = anchor ? anchor : selectedEntity;
		const bool valid = primary && world && world->IsValidWorldEntity(primary);
		const uint32_t id = valid ? static_cast<uint32_t>(primary.getId()) : UINT32_MAX;
		// 拖动结束（松手 / 取消）时清掉拖放状态，免得高亮残留
		if (!ImGui::GetDragDropPayload())
			EndDragHover(editorApp);
		std::vector<uint32_t> ids;
		for (const Entity& e : s_selection)
		{
			if (world && world->IsValidWorldEntity(e))
				ids.push_back(static_cast<uint32_t>(e.getId()));
		}
		// 脚本 / UI / 材质拖在网格上方：把它当成非主选中一起描边，提示会挂到哪个物体上
		if (Entity highlight = DragHighlightEntity(editorApp))
			ids.push_back(static_cast<uint32_t>(highlight.getId()));
		channel->SetSelection(std::move(ids), id);
	}

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
		// 落点：视口中心的网格表面（实体 ID 缓冲拾取，异步，一般 2 ~ 3 帧）。结果到之前菜单的请求保留，下一帧再来
		glm::vec3 position(0.f);
		const bool placed = ResolveAddPlacement(editorApp, viewportPos, viewportSize, position);

		if (placed && WantToAddDirLight)
		{
			editorApp->Scene().AddLight(LightType::Directional, position);
			WantToAddDirLight = false;
		}
		if (placed && WantToAddPointLight)
		{
			editorApp->Scene().AddLight(LightType::Point, position);
			WantToAddPointLight = false;
		}
		if (placed && WantToAddSpotLight)
		{
			editorApp->Scene().AddLight(LightType::Spot, position);
			WantToAddSpotLight = false;
		}
		if (placed && WantToAddCube)
		{
			editorApp->Scene().AddGeometry(GeometryType::Cube, position);
			WantToAddCube = false;
		}
		if (placed && WantToAddSphere)
		{
			editorApp->Scene().AddGeometry(GeometryType::Sphere, position);
			WantToAddSphere = false;
		}
		if (placed && WantToAddEmptyEntity)
		{
			editorApp->Scene().AddEmptyEntity(position);
			WantToAddEmptyEntity = false;
		}
		if (placed && WantToAddDDGIVolume)
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
			// 拖在视口上方的每一帧：持续拾取鼠标下的目标，松手时直接用（见 DragHoverPick）
			const ImVec2 dropMouse = ImGui::GetMousePos();
			const DropKind dropKind = ClassifyDragPayload(ImGui::GetDragDropPayload(), projectManager);
			if (dropKind != DropKind::None)
				UpdateDragHover(editorApp, dropKind, dropMouse, viewportPos, viewportSize);
			else
				EndDragHover(editorApp);

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0) {
					AssetGUID guid(data);
					if (auto meta = projectManager->GetDatabase()->GetAssetMetaByGuid(guid))
					{
						const ViewportDropTarget target = ResolveDropTarget(editorApp, dropMouse, viewportPos, viewportSize);
						HandleAssetDrop(*meta, editorApp, projectManager, viewportPos, viewportSize, target);
					}
				}
				EndDragHover(editorApp);
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
						const ViewportDropTarget target = ResolveDropTarget(editorApp, dropMouse, viewportPos, viewportSize);
						if (target.entity)
						{
							if (ext == ".ui")
								HandleUIAssetFileDrop(fileFullPath, target.entity, projectManager);
							else
								HandleScriptFileDrop(fileFullPath, target.entity, projectManager);
						}
					}
				}
				EndDragHover(editorApp);
			}
			ImGui::EndDragDropTarget();
		}
		else if (s_dragHover.hovering)
			EndDragHover(editorApp);		// 拖出视口

		// 相机矩阵取渲染线程最近一帧的快照（不再从生产者的三缓冲里抢）。
		glm::mat4 view(1.f), projection(1.f);
		glm::vec3 cameraPosition(0.f), cameraDirection(0.f, 0.f, -1.f);
		const bool hasCamera = editorApp->GetCameraSnapshot(view, projection, cameraPosition, cameraDirection);

		// 没有网格的 3D 实体的图标（先画，2D 包围盒和手柄盖在上面）。点选在下面的点击处理里，用本帧算出的屏幕矩形
		if (hasCamera && editorApp->GetLayerVisibility().layer3D)
		{
			glm::mat4 projGL = projection;
			projGL[1][1] *= -1;
			ViewportIcons::Draw(editorApp, s_selection, projGL * view, cameraPosition, viewportPos, viewportSize);
		}
		else
			ViewportIcons::Clear();

		// 选中物体的包围盒（先画，手柄盖在上面）：2D 精灵 / 动图画矩形，3D 静态模型画 OBB。对应的层关掉时不画
		if (!s_selection.empty() && s_showSelectionBounds)
		{
			const auto boundsVisibility = editorApp->GetLayerVisibility();
			if (boundsVisibility.layer2D)
				DrawSelection2DBounds(editorApp, s_selection, viewportPos, viewportSize);
			if (boundsVisibility.layer3D && hasCamera)
			{
				glm::mat4 projGL = projection;
				projGL[1][1] *= -1;
				DrawSelection3DBounds(editorApp, s_selection, projGL * view, viewportPos, viewportSize);
			}
		}

		// 物理碰撞体（Bullet 里实际的碰撞形状），画在手柄下面。3D 层关掉时不画
		if (hasCamera && editorApp->GetLayerVisibility().layer3D)
		{
			glm::mat4 projGL = projection;
			projGL[1][1] *= -1;
			PhysicsDebugView::Draw(editorApp, s_selection, projGL * view, cameraPosition, viewportPos, viewportSize);
		}

		// 手柄：单选沿用原来的规则；多选时按 ResolveGizmoTargets 选 3D / 2D，放在第一个有对应组件的实体上，改动作用于所有有该组件的选中实体
		const GizmoTargets gizmoTargets = ResolveGizmoTargets();
		if (gizmoTargets.anchor && (gizmoTargets.is2D || hasCamera))
		{
			if (gizmoTargets.is2D)
			{
				FinishGizmo3DDrag();
				Draw2DGizmo(editorApp, gizmoTargets, viewportSize, mCurrentGizmoOperation, mCurrentGizmoMode, useSnap ? snap : nullptr);
			}
			else
			{
				s_gizmo2DDrag.active = false;
				Draw3DGizmo(editorApp, gizmoTargets, view, projection, mCurrentGizmoOperation, mCurrentGizmoMode, useSnap ? snap : nullptr);
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
		else
		{
			// 没有可调整的实体（或者没有相机）：结束进行中的拖动（DDGI 体积照常收尾）
			FinishGizmo3DDrag();
			s_gizmo2DDrag.active = false;
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
			// 多选：删除全部选中（先弹确认框）；复制不可用
			const bool multi = s_selection.size() > 1;
			if (multi)
			{
				if (ImGui::MenuItem(std::format("Delete Selected ({})...", s_selection.size()).c_str()))
					RequestMultiDelete();
			}
			else if (ImGui::MenuItem("Delete"))
			{
				editorApp->GetWorld()->SubmitCommand([entity = selectedEntity, world = editorApp->GetWorld()]() -> void {
					if (!entity || !world)
						return;
					world->destroyEntity(entity);
					}
				);
				selectedEntity = Entity();
			}
			if (ImGui::MenuItem("Duplicate", nullptr, false, !multi))
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

					// DDGI 探针编辑模式下先点选探针；没被它用掉时先不选：松手时没拖动就是点选，拖动超过几个像素就是框选
					// （见 UpdateBoxSelect）。Ctrl = 切换、Shift = 加入，按下那一刻的修饰键为准
					if (!DDGIEditor::HandleViewportClick(selectedEntity, rayOrigin, rayDirection, io.KeyShift))
					{
						s_boxSelect.armed = true;
						s_boxSelect.dragging = false;
						s_boxSelect.start = mousePos;
						s_boxSelect.mode = SelectModeFromKeys();
					}
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

		// 点选 / 框选：松手时结算（框选时画框）
		UpdateBoxSelect(editorApp, viewportPos, viewportSize);

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

	// 多选删除的确认框（在所有窗口之外打开 / 绘制）
	DrawMultiDeleteConfirm(editorApp);
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
		if (s_selection.size() > 1) {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "选择实体: %d 个（第一个 %d）", static_cast<int>(s_selection.size()), selectedEntity.getId());
		}
		else if (selectedEntity) {
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

void ImguiLayout::HandleAssetDrop(const AssetMeta& meta, EditorApp* editorApp, ProjectManager* projectManager, const ImVec2& viewportPos, const ImVec2& viewportSize, const ViewportDropTarget& target)
{
	// 获取鼠标在场景中的世界坐标位置
	ImVec2 mousePos = ImGui::GetMousePos();
	//glm::vec3 worldPos = GetMouseWorldPosition(mousePos, editorApp, viewportPos, viewportSize);


	auto assetObject = projectManager->LoadAsset(meta.guid);
	if (!assetObject)
		return;

	// 拖放目标在拖动过程中就拾取好了（ResolveDropTarget）：
	//   模型摆在鼠标下的网格表面上，没有命中表面时摆在鼠标射线前方 15 米；脚本 / UI / 材质挂到鼠标下的实体上
	const glm::vec3 dropPosition = target.hasSurface ? target.surfacePosition : target.rayOrigin + target.rayDirection * 15.f;
	auto dropEntity = [&](World& world) -> Entity {
		return (target.entity && world.IsValidWorldEntity(target.entity)) ? target.entity : Entity();
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
						trans->position = dropPosition;

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
						trans->position = dropPosition;

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

					Entity entity = dropEntity(world);
					if (!entity)
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
						trans->position = dropPosition;

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
						trans->position = dropPosition;

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
		auto world = editorApp->GetWorld();
		if (Entity entity = world ? dropEntity(*world) : Entity())
			ApplyScriptAsset(entity, meta, projectManager);
		break;
	}
	case AssetType::UiDocument:
	{
		auto world = editorApp->GetWorld();
		if (Entity entity = world ? dropEntity(*world) : Entity())
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
