#include "Sprite2DEditor.h"

#include "AssetThumbnails.h"
#include "EditorApp.h"
#include "SceneEditor.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "Project/AssetObject.h"
#include "ECSCore/World.h"

#include <chrono>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace
{
	const char* AssetTypeLabel(AssetType type)
	{
		switch (type)
		{
		case AssetType::Texture: return "贴图";
		case AssetType::Sprite: return "精灵";
		case AssetType::AnimatedImage: return "动图";
		default: return "资产";
		}
	}

	ImVec2 FitInto(const glm::vec2& size, const ImVec2& box)
	{
		if (size.x <= 0.0f || size.y <= 0.0f || box.x <= 0.0f || box.y <= 0.0f)
			return ImVec2(0.0f, 0.0f);
		const float scale = std::min(box.x / size.x, box.y / size.y);
		return ImVec2(size.x * scale, size.y * scale);
	}

	// ---------------- Sprite 编辑器状态 ----------------
	struct SpriteEditorState
	{
		bool open = false;
		AssetGUID guid;
		bool originValid = false;
		std::vector<Render2D::SpriteRegion> originRegions;
		TextureConfig config;			// 导入配置（改完点应用才生效）
		int selected = -1;
		float zoom = 0.0f;			// <= 0：自动适配窗口
		bool dragDefine = false;	// 在图上拖拽框选当前区域
		bool dragging = false;
		ImVec2 dragStart;
		int gridCols = 4;
		int gridRows = 4;
		std::string message;
	};
	SpriteEditorState s_sprite;

	// ---------------- 动图预览状态 ----------------
	struct AnimatedViewerState
	{
		bool open = false;
		AssetGUID guid;
		bool playing = true;
		float speed = 1.0f;
		float timeMs = 0.0f;
		int frame = 0;
		TextureConfig config;
		bool configLoaded = false;
		std::string message;
	};
	AnimatedViewerState s_viewer;

	std::shared_ptr<SpriteAsset> LoadSpriteAsset(ProjectManager* projectManager, const AssetGUID& guid)
	{
		if (!projectManager || guid.empty())
			return nullptr;
		return std::dynamic_pointer_cast<SpriteAsset>(projectManager->LoadAsset(guid));
	}

	void RestoreSpriteOrigin(ProjectManager* projectManager)
	{
		if (!s_sprite.originValid)
			return;
		if (auto asset = LoadSpriteAsset(projectManager, s_sprite.guid))
			asset->SetRegions(s_sprite.originRegions);
	}
}

namespace Sprite2DEditor
{
	ImGuiTextureCache& TextureCache()
	{
		// 编辑器主窗口有帧并行：描述符集推迟 3 帧再释放
		static ImGuiTextureCache cache(3u, 600u, true);
		return cache;
	}

	float EditorTimeMs()
	{
		static const auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	void DrawFrameInRect(ImDrawList* drawList, const Render2D::SpriteFrame& frame, const ImVec2& p0, const ImVec2& p1, ImU32 color)
	{
		if (!drawList || !frame.IsValid())
			return;

		const ImTextureID id = TextureCache().Get(frame.texture);
		if (id == ImTextureID_Invalid)
			return;

		const ImVec2 box(p1.x - p0.x, p1.y - p0.y);
		const ImVec2 size = FitInto(frame.size, box);
		const ImVec2 a(p0.x + (box.x - size.x) * 0.5f, p0.y + (box.y - size.y) * 0.5f);
		const ImVec2 b(a.x + size.x, a.y + size.y);

		const bool nearest = ImGuiTextureCache::WantsNearest(*frame.texture);
		ImGuiPlatformIO& io = ImGui::GetPlatformIO();
		if (nearest && io.DrawCallback_SetSamplerNearest)
			drawList->AddCallback(io.DrawCallback_SetSamplerNearest, nullptr);
		drawList->AddImage(id, a, b, ImVec2(frame.uvRect.x, frame.uvRect.y), ImVec2(frame.uvRect.z, frame.uvRect.w), color);
		if (nearest && io.DrawCallback_SetSamplerLinear)
			drawList->AddCallback(io.DrawCallback_SetSamplerLinear, nullptr);
	}

	void DrawFramePreview(const Render2D::SpriteFrame& frame, const ImVec2& maxSize)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + maxSize.x, p0.y + maxSize.y);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// 棋盘格底：看得出透明区域
		const float cell = 8.0f;
		for (float y = p0.y; y < p1.y; y += cell)
		{
			for (float x = p0.x; x < p1.x; x += cell)
			{
				const bool dark = (static_cast<int>((x - p0.x) / cell) + static_cast<int>((y - p0.y) / cell)) % 2 == 0;
				drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, p1.x), std::min(y + cell, p1.y)),
					dark ? IM_COL32(60, 60, 60, 255) : IM_COL32(90, 90, 90, 255));
			}
		}

		if (frame.IsValid())
			DrawFrameInRect(drawList, frame, p0, p1);
		else
			drawList->AddText(ImVec2(p0.x + 6.0f, p0.y + 6.0f), IM_COL32(200, 200, 200, 255), "无预览");

		drawList->AddRect(p0, p1, IM_COL32(120, 120, 120, 255));
		ImGui::Dummy(maxSize);
	}


	bool DrawAssetSlot(const char* label, AssetGUID& guid, std::initializer_list<AssetType> accepted, ProjectManager* projectManager,
		std::initializer_list<AssetType> convertible, const std::function<void(const AssetMeta&)>& onConvert)
	{
		bool changed = false;
		ImGui::PushID(label);

		auto database = projectManager ? projectManager->GetDatabase() : nullptr;
		auto meta = (database && !guid.empty()) ? database->GetAssetMetaByGuid(guid) : nullptr;

		std::string accepts;
		for (AssetType type : accepted)
		{
			if (!accepts.empty()) accepts += " / ";
			accepts += AssetTypeLabel(type);
		}

		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		if (guid.empty())
			ImGui::TextDisabled("未引用");
		else if (!meta)
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "资产不存在 (%s)", guid.c_str());
		else
		{
			ImGui::TextUnformatted(meta->name.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s\n%s", meta->path.c_str(), meta->guid.c_str());
		}

		std::string converts;
		for (AssetType type : convertible)
		{
			if (!converts.empty()) converts += " / ";
			converts += AssetTypeLabel(type);
		}

		const std::string buttonText = std::format("拖入 {} 资产替换", accepts);
		if (ImGui::Button(buttonText.c_str(), ImVec2(-60.0f, 0.0f))) {}
		if (!converts.empty() && ImGui::IsItemHovered())
			ImGui::SetTooltip("也可以拖入%s资产：先用它的原图生成%s，导入完成后填到这里", converts.c_str(), accepts.c_str());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const char* data = static_cast<const char*>(payload->Data);
				auto dropped = (data && *data && database) ? database->GetAssetMetaByGuid(AssetGUID(data)) : nullptr;
				if (dropped && std::find(accepted.begin(), accepted.end(), dropped->type) != accepted.end())
				{
					guid = dropped->guid;
					changed = true;
				}
				else if (dropped && onConvert && std::find(convertible.begin(), convertible.end(), dropped->type) != convertible.end())
				{
					onConvert(*dropped);
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::Button("清除", ImVec2(-FLT_MIN, 0.0f)) && !guid.empty())
		{
			guid.clear();
			changed = true;
		}

		ImGui::PopID();
		return changed;
	}

	bool Create2DEntity(World& world, const AssetMeta& meta, const glm::vec2& position)
	{
		if (meta.type != AssetType::Sprite && meta.type != AssetType::AnimatedImage)
			return false;

		Entity entity = world.createEntity();
		entity.addComponent<Transform2D>(position);
		if (meta.type == AssetType::AnimatedImage)
		{
			auto& player = entity.addComponent<AnimatedImagePlayer>();
			player.imageGuid = meta.guid;
			SetNameTag(entity, std::format("animated_image_{}", entity.getId()));
		}
		else
		{
			auto& sprite = entity.addComponent<Sprite>();
			sprite.spriteGuid = meta.guid;
			SetNameTag(entity, std::format("sprite_{}", entity.getId()));
		}
		return true;
	}

	void OpenSpriteEditor(const AssetGUID& guid)
	{
		if (s_sprite.open && s_sprite.guid == guid)
			return;
		// 换资产时，没保存的改动还原
		if (s_sprite.open)
			RestoreSpriteOrigin(EditorApp::Instance()->GetProjectManager());

		s_sprite = SpriteEditorState{};
		s_sprite.open = true;
		s_sprite.guid = guid;
	}

	void OpenAnimatedImageViewer(const AssetGUID& guid)
	{
		s_viewer = AnimatedViewerState{};
		s_viewer.open = true;
		s_viewer.guid = guid;
	}
}

namespace
{
	// ---------------- 两个编辑器共用的布局工具（和材质编辑器同一套：分栏 + 左标签右控件 + 底栏） ----------------
	constexpr float kSectionLabelWidth = 96.0f;

	// 分栏标题（可折叠，默认展开）+ 两列表格；返回 false 时不用 EndSection
	bool BeginSection(const char* title, float labelWidth = kSectionLabelWidth)
	{
		if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
			return false;
		if (!ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
			return false;
		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
		return true;
	}
	void EndSection() { ImGui::EndTable(); }

	// 不带标题的两列表格（分栏里的一组行）；返回 false 时不用 EndSection
	bool BeginRows(const char* id, float labelWidth = kSectionLabelWidth)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
			return false;
		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
		return true;
	}

	// 一行：左标签（可带悬停说明），进入右列，控件宽度占满
	void SectionRow(const char* label, const char* tooltip = nullptr)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	// 一行只读文字
	void SectionText(const char* label, const std::string& value, bool disabled = false)
	{
		SectionRow(label);
		ImGui::AlignTextToFramePadding();
		if (disabled)
			ImGui::TextDisabled("%s", value.c_str());
		else
			ImGui::TextUnformatted(value.c_str());
		if (ImGui::IsItemHovered() && ImGui::CalcTextSize(value.c_str()).x > ImGui::GetContentRegionAvail().x)
			ImGui::SetTooltip("%s", value.c_str());
	}

	// 导入配置（过滤 / sRGB）两行，返回是否改了
	bool ImportConfigRows(TextureConfig& config)
	{
		bool edited = false;
		int filter = config.magFilter == vk::Filter::eNearest ? 0 : 1;
		const char* filters[] = { "Nearest（像素风）", "Linear（平滑）" };
		SectionRow("过滤", "放大 / 缩小采样方式");
		if (ImGui::Combo("##Filter", &filter, filters, IM_ARRAYSIZE(filters)))
		{
			config.minFilter = filter == 0 ? vk::Filter::eNearest : vk::Filter::eLinear;
			config.magFilter = config.minFilter;
			edited = true;
		}
		SectionRow("sRGB", "图片是 sRGB 颜色（一般的彩色图片都勾上；法线、遮罩之类的数据图不勾）");
		edited |= ImGui::Checkbox("##Gamma", &config.gammaCorrection);
		return edited;
	}

	bool SameImportConfig(const TextureConfig& a, const TextureConfig& b)
	{
		return a.magFilter == b.magFilter && a.minFilter == b.minFilter && a.gammaCorrection == b.gammaCorrection;
	}

	bool SameRegions(const std::vector<Render2D::SpriteRegion>& a, const std::vector<Render2D::SpriteRegion>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].name != b[i].name || a[i].rect != b[i].rect || a[i].pivot != b[i].pivot || a[i].border != b[i].border)
				return false;
		}
		return true;
	}

	// 底栏：左边状态（错误红字 / 未保存黄字 / 其他灰字），右边靠右显示资产路径
	void DrawStatusAndPath(const std::string& message, bool error, bool dirty, const std::string& path)
	{
		ImGui::AlignTextToFramePadding();
		if (!message.empty())
			ImGui::TextColored(error ? ImVec4(1.f, 0.35f, 0.35f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", message.c_str());
		else if (dirty)
			ImGui::TextColored(ImVec4(1.f, 0.78f, 0.3f, 1.f), "● 有未保存的修改");
		else
			ImGui::TextDisabled("已保存");

		const float pathWidth = ImGui::CalcTextSize(path.c_str()).x;
		ImGui::SameLine();
		const float spare = ImGui::GetContentRegionAvail().x - pathWidth;
		if (spare > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spare);
		ImGui::TextDisabled("%s", path.c_str());
	}

	// ============================== 精灵编辑器 ==============================
	// 布局：左边画布（工具条 + 图 + 区域框），右边分栏（图片 / 切分 / 区域列表 / 选中区域），底栏保存 / 还原 + 状态 + 路径。
	// 窗口窄时上下排：画布在上，分栏在下
	void DrawSpriteEditor(ProjectManager* projectManager)
	{
		if (!s_sprite.open)
			return;

		auto asset = LoadSpriteAsset(projectManager, s_sprite.guid);
		auto database = projectManager ? projectManager->GetDatabase() : nullptr;
		auto meta = database ? database->GetAssetMetaByGuid(s_sprite.guid) : nullptr;
		if (!asset || !meta)
		{
			s_sprite.open = false;
			return;
		}

		if (!s_sprite.originValid)
		{
			s_sprite.originRegions = asset->GetRegions();
			s_sprite.config = asset->GetConfig();
			s_sprite.originValid = true;
		}

		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(980.0f, 640.0f), ImGuiCond_FirstUseEver);
		const std::string title = std::format("精灵编辑器 - {}###SpriteEditor", meta->name);
		if (!ImGui::Begin(title.c_str(), &open))
		{
			ImGui::End();
			if (!open) { RestoreSpriteOrigin(projectManager); s_sprite.open = false; }
			return;
		}

		auto regions = asset->GetRegions();
		bool regionsChanged = false;
		static bool s_messageIsError = false;

		auto sprite = asset->AsSprite();
		auto texture = sprite ? sprite->GetTexture() : nullptr;
		const glm::vec2 texSize = texture ? glm::vec2(texture->GetSize()) : glm::vec2(0.0f);
		const bool hasSelection = s_sprite.selected >= 0 && s_sprite.selected < static_cast<int>(regions.size());
		const ImGuiStyle& style = ImGui::GetStyle();

		// ---------------- 画布：工具条 + 图 ----------------
		auto drawCanvas = [&](float height)
			{
				// 工具条：缩放、适配、1:1、框选模式
				float zoomValue = s_sprite.zoom;
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::SliderFloat("##Zoom", &zoomValue, 0.0f, 16.0f, s_sprite.zoom > 0.0f ? "缩放 %.2fx" : "缩放：适配"))
					s_sprite.zoom = zoomValue;
				ImGui::SameLine();
				if (ImGui::Button("适配"))
					s_sprite.zoom = 0.0f;
				ImGui::SameLine();
				if (ImGui::Button("1:1"))
					s_sprite.zoom = 1.0f;
				ImGui::SameLine();
				ImGui::BeginDisabled(!hasSelection);
				ImGui::Checkbox("框选模式", &s_sprite.dragDefine);
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", hasSelection ? "在图上拖出一个框，设为选中区域的矩形（吸附到整像素）" : "先在右边选中一个区域");
				ImGui::SameLine();
				ImGui::TextDisabled("Ctrl + 滚轮缩放 · 点区域选中");

				ImGui::BeginChild("##SpriteCanvas", ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
				if (texture && texSize.x > 0.0f && texSize.y > 0.0f)
				{
					const ImVec2 avail = ImGui::GetContentRegionAvail();
					const float fitZoom = std::max(0.01f, std::min(avail.x / texSize.x, avail.y / texSize.y));
					const float zoom = s_sprite.zoom > 0.0f ? s_sprite.zoom : fitZoom;
					const ImVec2 origin = ImGui::GetCursorScreenPos();
					const ImVec2 canvas(texSize.x * zoom, texSize.y * zoom);
					ImDrawList* drawList = ImGui::GetWindowDrawList();

					Render2D::SpriteFrame whole = Render2D::Sprite::MakeFrame(texture, nullptr);
					drawList->AddRectFilled(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), IM_COL32(50, 50, 50, 255));
					Sprite2DEditor::DrawFrameInRect(drawList, whole, origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y));
					ImGui::InvisibleButton("##canvas", canvas);
					const bool canvasHovered = ImGui::IsItemHovered();

					auto toScreen = [&](float x, float y) { return ImVec2(origin.x + x * zoom, origin.y + y * zoom); };
					for (int i = 0; i < static_cast<int>(regions.size()); ++i)
					{
						const auto& r = regions[i];
						const bool selected = i == s_sprite.selected;
						const ImU32 color = selected ? IM_COL32(255, 200, 40, 255) : IM_COL32(80, 200, 255, 200);
						const ImVec2 a = toScreen(r.rect.x, r.rect.y);
						const ImVec2 b = toScreen(r.rect.x + r.rect.z, r.rect.y + r.rect.w);
						drawList->AddRect(a, b, color, 0.0f, 0, selected ? 2.0f : 1.0f);
						if (selected)
						{
							// 九宫格边距线 + 轴心
							const ImU32 sliceColor = IM_COL32(120, 255, 120, 200);
							if (r.border.x > 0) drawList->AddLine(toScreen(r.rect.x + r.border.x, r.rect.y), toScreen(r.rect.x + r.border.x, r.rect.y + r.rect.w), sliceColor);
							if (r.border.z > 0) drawList->AddLine(toScreen(r.rect.x + r.rect.z - r.border.z, r.rect.y), toScreen(r.rect.x + r.rect.z - r.border.z, r.rect.y + r.rect.w), sliceColor);
							if (r.border.y > 0) drawList->AddLine(toScreen(r.rect.x, r.rect.y + r.border.y), toScreen(r.rect.x + r.rect.z, r.rect.y + r.border.y), sliceColor);
							if (r.border.w > 0) drawList->AddLine(toScreen(r.rect.x, r.rect.y + r.rect.w - r.border.w), toScreen(r.rect.x + r.rect.z, r.rect.y + r.rect.w - r.border.w), sliceColor);
							drawList->AddCircleFilled(toScreen(r.rect.x + r.rect.z * r.pivot.x, r.rect.y + r.rect.w * r.pivot.y), 4.0f, IM_COL32(255, 80, 80, 255));
						}
						else if (canvasHovered && !s_sprite.dragDefine && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						{
							const ImVec2 m = ImGui::GetMousePos();
							if (m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y)
								s_sprite.selected = i;
						}
					}

					// 拖拽框选当前区域（坐标吸附到整像素）
					const bool canDefine = s_sprite.dragDefine && s_sprite.selected >= 0 && s_sprite.selected < static_cast<int>(regions.size());
					auto toPixel = [&](const ImVec2& p)
						{
							return glm::vec2(std::clamp(std::round((p.x - origin.x) / zoom), 0.0f, texSize.x), std::clamp(std::round((p.y - origin.y) / zoom), 0.0f, texSize.y));
						};
					if (canDefine && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						s_sprite.dragging = true;
						s_sprite.dragStart = ImGui::GetMousePos();
					}
					if (s_sprite.dragging)
					{
						const glm::vec2 a = toPixel(s_sprite.dragStart);
						const glm::vec2 b = toPixel(ImGui::GetMousePos());
						const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
						drawList->AddRect(toScreen(lo.x, lo.y), toScreen(hi.x, hi.y), IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.5f);
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
						{
							s_sprite.dragging = false;
							if (canDefine && hi.x - lo.x >= 1.0f && hi.y - lo.y >= 1.0f)
							{
								regions[s_sprite.selected].rect = glm::vec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
								regionsChanged = true;
							}
						}
					}

					// Ctrl + 滚轮缩放
					if (canvasHovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
						s_sprite.zoom = std::clamp(zoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.25f : 0.8f), 0.05f, 32.0f);
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "图片加载失败（看控制台输出）。旧版引用贴图资产的精灵需要重新导入");
				}
				ImGui::EndChild();
			};

		// ---------------- 分栏 ----------------
		auto drawSections = [&]()
			{
				// ---- 图片 ----
				if (BeginSection("图片"))
				{
					SectionText("文件", asset->GetPath());
					SectionText("尺寸", std::format("{:.0f} × {:.0f} px", texSize.x, texSize.y));
					ImportConfigRows(s_sprite.config);

					const bool configDirty = !SameImportConfig(asset->GetConfig(), s_sprite.config);
					SectionRow("");
					ImGui::BeginDisabled(!configDirty);
					if (ImGui::Button("应用导入配置"))
					{
						const std::string source = projectManager->GetProjectAssetFullPath(asset->GetPath());
						if (asset->SetConfig(s_sprite.config, source) && projectManager->SaveAsset(s_sprite.guid))
						{
							AssetThumbnails::Invalidate(s_sprite.guid);
							s_sprite.originRegions = asset->GetRegions();
							s_sprite.message = "导入配置已应用并保存";
							s_messageIsError = false;
						}
						else
						{
							s_sprite.message = "导入配置应用失败";
							s_messageIsError = true;
						}
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("按新配置重新加载图片并保存（连同当前的区域一起保存）");
					EndSection();
				}

				// ---- 切分 ----
				if (BeginSection("切分"))
				{
					SectionRow("网格", "列 × 行：按行列均分整张图片（图集 / 序列帧）");
					ImGui::DragInt2("##Grid", &s_sprite.gridCols, 0.1f, 1, 256, "%d", ImGuiSliderFlags_AlwaysClamp);
					s_sprite.gridCols = std::max(1, s_sprite.gridCols);
					s_sprite.gridRows = std::max(1, s_sprite.gridRows);
					if (texSize.x > 0.0f && texSize.y > 0.0f)
					{
						SectionText("每格", std::format("{:.0f} × {:.0f} px，共 {} 个区域",
							std::floor(texSize.x / s_sprite.gridCols), std::floor(texSize.y / s_sprite.gridRows), s_sprite.gridCols * s_sprite.gridRows), true);
					}
					SectionRow("");
					ImGui::BeginDisabled(texSize.x <= 0.0f || texSize.y <= 0.0f);
					if (ImGui::Button("按网格切分"))
					{
						regions.clear();
						const float cw = std::floor(texSize.x / s_sprite.gridCols);
						const float ch = std::floor(texSize.y / s_sprite.gridRows);
						for (int r = 0; r < s_sprite.gridRows; ++r)
							for (int c = 0; c < s_sprite.gridCols; ++c)
							{
								Render2D::SpriteRegion region;
								region.name = std::format("{}_{}", fs::path(meta->name).stem().string(), r * s_sprite.gridCols + c);
								region.rect = glm::vec4(c * cw, r * ch, cw, ch);
								regions.push_back(region);
							}
						s_sprite.selected = regions.empty() ? -1 : 0;
						regionsChanged = true;
					}
					ImGui::EndDisabled();
					if (!regions.empty())
						ImGui::SetItemTooltip("会覆盖现有的 %zu 个区域（没保存前可以还原）", regions.size());
					EndSection();
				}

				// ---- 区域列表 ----
				const std::string regionsTitle = std::format("区域（{}）###Regions", regions.size());
				if (ImGui::CollapsingHeader(regionsTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::Button("+ 添加"))
					{
						Render2D::SpriteRegion region;
						region.name = std::format("region_{}", regions.size());
						region.rect = glm::vec4(0.0f, 0.0f, texSize.x, texSize.y);
						regions.push_back(region);
						s_sprite.selected = static_cast<int>(regions.size()) - 1;
						regionsChanged = true;
					}
					ImGui::SameLine();
					const bool canRemove = s_sprite.selected >= 0 && s_sprite.selected < static_cast<int>(regions.size());
					ImGui::BeginDisabled(!canRemove);
					if (ImGui::Button("删除"))
					{
						regions.erase(regions.begin() + s_sprite.selected);
						s_sprite.selected = std::min(s_sprite.selected, static_cast<int>(regions.size()) - 1);
						regionsChanged = true;
					}
					ImGui::EndDisabled();

					if (regions.empty())
						ImGui::TextDisabled("没有区域：整张图片就是一帧");
					else
					{
						// 固定高度（最多 8 行）、底边可以拖动调高；只画看得见的行（网格切分可能有几百个）
						const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
						const float listHeight = std::min<float>(static_cast<float>(regions.size()), 8.0f) * rowHeight + style.WindowPadding.y * 2.0f;
						if (ImGui::BeginChild("##RegionList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
						{
							ImGuiListClipper clipper;
							clipper.Begin(static_cast<int>(regions.size()));
							while (clipper.Step())
							{
								for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
								{
									ImGui::PushID(i);
									const auto& region = regions[i];
									const std::string label = region.name.empty() ? std::format("<未命名 {}>", i) : region.name;
									const ImVec2 rowStart = ImGui::GetCursorScreenPos();
									if (ImGui::Selectable(label.c_str(), s_sprite.selected == i, ImGuiSelectableFlags_AllowOverlap))
										s_sprite.selected = i;
									// 右边靠右：尺寸
									const std::string size = std::format("{:.0f}×{:.0f}", region.rect.z, region.rect.w);
									const float sizeWidth = ImGui::CalcTextSize(size.c_str()).x;
									ImGui::GetWindowDrawList()->AddText(
										ImVec2(rowStart.x + ImGui::GetContentRegionAvail().x - sizeWidth, rowStart.y),
										ImGui::GetColorU32(ImGuiCol_TextDisabled), size.c_str());
									ImGui::PopID();
								}
							}
						}
						ImGui::EndChild();
					}
				}

				// ---- 选中区域 ----
				if (ImGui::CollapsingHeader("选中区域", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (!(s_sprite.selected >= 0 && s_sprite.selected < static_cast<int>(regions.size())))
						ImGui::TextDisabled("在上面的列表或画布上选一个区域");
					else
					{
						auto& region = regions[s_sprite.selected];
						ImGui::PushID("selected");

						// 头部：预览 + 名称
						constexpr float kPreviewSide = 96.0f;
						Render2D::SpriteFrame preview = Render2D::Sprite::MakeFrame(texture, &region);
						Sprite2DEditor::DrawFramePreview(preview, ImVec2(kPreviewSide, kPreviewSide));
						ImGui::SameLine();
						ImGui::BeginGroup();
						char nameBuffer[128]{};
						strncpy_s(nameBuffer, region.name.c_str(), _TRUNCATE);
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::InputTextWithHint("##Name", "区域名称", nameBuffer, sizeof(nameBuffer)))
						{
							region.name = nameBuffer;
							regionsChanged = true;
						}
						ImGui::TextDisabled("#%d · %.0f × %.0f px", s_sprite.selected, region.rect.z, region.rect.w);
						if (ImGui::Button("设为整张图片"))
						{
							region.rect = glm::vec4(0.0f, 0.0f, texSize.x, texSize.y);
							regionsChanged = true;
						}
						ImGui::EndGroup();

						const float maxSide = std::max(texSize.x, texSize.y);
						if (BeginRows("##RegionRect"))
						{
							SectionRow("位置", "图片上的像素坐标（左上角），原点在图片左上角");
							regionsChanged |= ImGui::DragFloat2("##Pos", &region.rect.x, 1.0f, 0.0f, maxSide, "%.0f");
							SectionRow("尺寸", "宽 × 高（像素）");
							regionsChanged |= ImGui::DragFloat2("##Size", &region.rect.z, 1.0f, 0.0f, maxSide, "%.0f");
							EndSection();
						}

						// 轴心：数值 + 常用位置
						ImGui::SeparatorText("轴心");
						if (BeginRows("##RegionPivot"))
						{
							SectionRow("轴心", "按比例：(0,0) 左上，(0.5,0.5) 中心，(0.5,1) 底边中点。\n实体的位置就是轴心所在的点，旋转和缩放也绕它进行");
							regionsChanged |= ImGui::DragFloat2("##Pivot", &region.pivot.x, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
							SectionRow("常用");
							const std::pair<const char*, glm::vec2> presets[] = {
								{ "左上", { 0.0f, 0.0f } }, { "中心", { 0.5f, 0.5f } }, { "底边中点", { 0.5f, 1.0f } }, { "左下", { 0.0f, 1.0f } } };
							for (size_t k = 0; k < std::size(presets); ++k)
							{
								if (k > 0)
									ImGui::SameLine();
								const bool active = region.pivot == presets[k].second;
								if (active)
									ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
								if (ImGui::SmallButton(presets[k].first))
								{
									region.pivot = presets[k].second;
									regionsChanged = true;
								}
								if (active)
									ImGui::PopStyleColor();
							}
							EndSection();
						}

						// 九宫格
						ImGui::SeparatorText("九宫格");
						if (BeginRows("##RegionSlice"))
						{
							const float sliceMax = std::max(region.rect.z, region.rect.w);
							SectionRow("左 / 右", "左右边距（像素）：组件绘制模式选九宫格时生效，四角保持原尺寸，四边单向拉伸，中间双向拉伸");
							float lr[2] = { region.border.x, region.border.z };
							if (ImGui::DragFloat2("##LR", lr, 0.5f, 0.0f, sliceMax, "%.0f"))
							{
								region.border.x = lr[0];
								region.border.z = lr[1];
								regionsChanged = true;
							}
							SectionRow("上 / 下", "上下边距（像素）");
							float tb[2] = { region.border.y, region.border.w };
							if (ImGui::DragFloat2("##TB", tb, 0.5f, 0.0f, sliceMax, "%.0f"))
							{
								region.border.y = tb[0];
								region.border.w = tb[1];
								regionsChanged = true;
							}
							EndSection();
						}
						ImGui::PopID();
					}
				}
			};

		// ---------------- 布局：宽窗口左画布、右分栏（中间可拖动），窄窗口上下排 ----------------
		const float footerHeight = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
		ImGui::BeginChild("##SpriteEditorContent", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const float toolbarHeight = ImGui::GetFrameHeightWithSpacing();
		if (ImGui::GetContentRegionAvail().x >= 640.0f)
		{
			if (ImGui::BeginTable("##SpriteEditorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch, 0.64f);
				ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.36f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				drawCanvas(std::max(64.0f, ImGui::GetContentRegionAvail().y - toolbarHeight));
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginChild("##SpriteProperties", ImVec2(0.0f, 0.0f));
				drawSections();
				ImGui::EndChild();
				ImGui::EndTable();
			}
		}
		else
		{
			drawCanvas(std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f - toolbarHeight));
			ImGui::BeginChild("##SpriteProperties", ImVec2(0.0f, 0.0f));
			drawSections();
			ImGui::EndChild();
		}
		ImGui::EndChild();

		// 改动立即作用到运行时对象：场景里引用这个 Sprite 的实体马上能看到
		if (regionsChanged)
		{
			asset->SetRegions(regions);
			s_sprite.message.clear();
		}

		// ---------------- 底栏：保存 / 还原（没改动时灰掉）+ 状态 + 路径 ----------------
		ImGui::Separator();
		const bool dirty = !SameRegions(asset->GetRegions(), s_sprite.originRegions);
		ImGui::BeginDisabled(!dirty);
		if (ImGui::Button("保存"))
		{
			if (projectManager->SaveAsset(s_sprite.guid))
			{
				s_sprite.originRegions = asset->GetRegions();
				s_sprite.message = "已保存";
				s_messageIsError = false;
				AssetThumbnails::Invalidate(s_sprite.guid);
			}
			else
			{
				s_sprite.message = "保存失败";
				s_messageIsError = true;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("还原"))
		{
			RestoreSpriteOrigin(projectManager);
			s_sprite.message = "已还原到上次保存";
			s_messageIsError = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		DrawStatusAndPath(dirty && !s_messageIsError ? std::string() : s_sprite.message, s_messageIsError, dirty, meta->path);

		ImGui::End();

		if (!open)
		{
			RestoreSpriteOrigin(projectManager);
			s_sprite.open = false;
		}
	}

	// ============================== 动图编辑器 ==============================
	// 布局：左边预览（最大的正方形）+ 播放条，右边分栏（信息 / 预览 / 导入），底栏状态 + 路径。窄窗口上下排
	void DrawAnimatedImageViewer(ProjectManager* projectManager)
	{
		if (!s_viewer.open)
			return;

		auto asset = projectManager ? std::dynamic_pointer_cast<AnimatedImageAsset>(projectManager->LoadAsset(s_viewer.guid)) : nullptr;
		auto database = projectManager ? projectManager->GetDatabase() : nullptr;
		auto meta = database ? database->GetAssetMetaByGuid(s_viewer.guid) : nullptr;
		if (!asset || !meta)
		{
			s_viewer.open = false;
			return;
		}
		if (!s_viewer.configLoaded)
		{
			s_viewer.config = asset->GetDescription().GetConfig();
			s_viewer.configLoaded = true;
		}

		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
		const std::string title = std::format("动图编辑器 - {}###AnimatedImageViewer", meta->name);
		if (!ImGui::Begin(title.c_str(), &open))
		{
			ImGui::End();
			if (!open)
				s_viewer.open = false;
			return;
		}

		static bool s_messageIsError = false;
		const ImGuiStyle& style = ImGui::GetStyle();
		auto image = asset->AsAnimatedImage();
		const bool valid = image && image->GetFrameCount() > 0;
		const uint32_t frameCount = valid ? image->GetFrameCount() : 0u;
		if (valid)
			s_viewer.frame = std::clamp(s_viewer.frame, 0, static_cast<int>(frameCount) - 1);

		// 跳到某一帧（停止播放，时间对齐到这一帧的开头）
		auto seekFrame = [&](int frame)
			{
				if (!valid)
					return;
				s_viewer.frame = std::clamp(frame, 0, static_cast<int>(frameCount) - 1);
				s_viewer.playing = false;
				float t = 0.0f;
				for (int i = 0; i < s_viewer.frame; ++i)
					t += image->GetFrame(static_cast<uint32_t>(i)).durationMs;
				s_viewer.timeMs = t;
			};

		if (valid && s_viewer.playing)
		{
			s_viewer.timeMs += ImGui::GetIO().DeltaTime * 1000.0f * s_viewer.speed;
			if (image->GetTotalDuration() > 0.0f)
				s_viewer.timeMs = std::fmod(std::max(0.0f, s_viewer.timeMs), image->GetTotalDuration());
			s_viewer.frame = static_cast<int>(image->FrameIndexAt(s_viewer.timeMs, 0));
		}

		// ---------------- 预览 + 播放条 ----------------
		auto drawPreview = [&](float maxHeight)
			{
				if (!valid)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "动图加载失败（看控制台输出）");
					return;
				}
				const float transportHeight = ImGui::GetFrameHeightWithSpacing();
				const ImVec2 avail = ImGui::GetContentRegionAvail();
				const float side = std::floor(std::max(64.0f, std::min(avail.x, maxHeight - transportHeight)));
				if (avail.x > side)
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail.x - side) * 0.5f));
				Sprite2DEditor::DrawFramePreview(image->GetFrameSprite(static_cast<uint32_t>(s_viewer.frame)), ImVec2(side, side));

				// 播放条：[◀] [播放/暂停] [▶] [====帧====]
				if (ImGui::ArrowButton("##Prev", ImGuiDir_Left))
					seekFrame(s_viewer.frame - 1);
				ImGui::SetItemTooltip("上一帧");
				ImGui::SameLine();
				if (ImGui::Button(s_viewer.playing ? "暂停" : "播放", ImVec2(ImGui::CalcTextSize("暂停").x + style.FramePadding.x * 2.0f, 0.0f)))
					s_viewer.playing = !s_viewer.playing;
				ImGui::SameLine();
				if (ImGui::ArrowButton("##Next", ImGuiDir_Right))
					seekFrame(s_viewer.frame + 1);
				ImGui::SetItemTooltip("下一帧");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-FLT_MIN);
				int frame = s_viewer.frame;
				const std::string frameFormat = std::format("第 %d / {} 帧", frameCount - 1);
				if (ImGui::SliderInt("##Frame", &frame, 0, static_cast<int>(frameCount) - 1, frameFormat.c_str()))
					seekFrame(frame);
			};

		// ---------------- 分栏 ----------------
		auto drawSections = [&]()
			{
				if (BeginSection("信息"))
				{
					if (valid)
					{
						SectionText("帧数", std::format("{} 帧", frameCount));
						SectionText("帧尺寸", std::format("{} × {} px", image->GetFrameSize().x, image->GetFrameSize().y));
						SectionText("一轮时长", std::format("{:.0f} ms", image->GetTotalDuration()));
						SectionText("图集", std::format("{} 页", image->GetPages().size()));
						SectionText("当前帧", std::format("#{} · {:.0f} ms", s_viewer.frame, image->GetFrame(static_cast<uint32_t>(s_viewer.frame)).durationMs));
					}
					SectionText("文件", asset->GetDescription().GetPath());
					EndSection();
				}

				if (BeginSection("预览"))
				{
					SectionRow("速度", "只影响这里的预览");
					ImGui::SliderFloat("##Speed", &s_viewer.speed, 0.1f, 4.0f, "%.2fx");
					SectionRow("");
					ImGui::TextDisabled("循环次数、播放速度在 AnimatedImagePlayer 组件 / UI 节点上设置");
					EndSection();
				}

				if (BeginSection("导入"))
				{
					ImportConfigRows(s_viewer.config);
					const bool configDirty = !SameImportConfig(asset->GetDescription().GetConfig(), s_viewer.config);
					SectionRow("");
					ImGui::BeginDisabled(!configDirty);
					if (ImGui::Button("应用导入配置"))
					{
						const std::string source = projectManager->GetProjectAssetFullPath(asset->GetDescription().GetPath());
						if (asset->SetConfig(s_viewer.config, source) && projectManager->SaveAsset(s_viewer.guid))
						{
							AssetThumbnails::Invalidate(s_viewer.guid);
							s_viewer.message = "已应用并保存（场景里的播放器下次解析时换成新图集）";
							s_messageIsError = false;
						}
						else
						{
							s_viewer.message = "应用失败";
							s_messageIsError = true;
						}
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("按新配置重新解码并保存");
					EndSection();
				}
			};

		// ---------------- 布局 ----------------
		const float footerHeight = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
		ImGui::BeginChild("##AnimatedContent", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (ImGui::GetContentRegionAvail().x >= 560.0f)
		{
			if (ImGui::BeginTable("##AnimatedLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.58f);
				ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.42f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				drawPreview(ImGui::GetContentRegionAvail().y);
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginChild("##AnimatedProperties", ImVec2(0.0f, 0.0f));
				drawSections();
				ImGui::EndChild();
				ImGui::EndTable();
			}
		}
		else
		{
			ImGui::BeginChild("##AnimatedSingle", ImVec2(0.0f, 0.0f));
			drawPreview(std::max(160.0f, ImGui::GetContentRegionAvail().y * 0.6f));
			ImGui::Spacing();
			drawSections();
			ImGui::EndChild();
		}
		ImGui::EndChild();

		// ---------------- 底栏：状态 + 路径（动图本身没有要保存的编辑，只有导入配置） ----------------
		ImGui::Separator();
		const bool configDirty = !SameImportConfig(asset->GetDescription().GetConfig(), s_viewer.config);
		DrawStatusAndPath(configDirty ? std::string("导入配置有改动，点\"应用导入配置\"生效") : s_viewer.message,
			!configDirty && s_messageIsError, false, meta->path);

		ImGui::End();

		if (!open)
			s_viewer.open = false;
	}
}

namespace Sprite2DEditor
{
	void DrawWindows(ProjectManager* projectManager)
	{
		DrawSpriteEditor(projectManager);
		DrawAnimatedImageViewer(projectManager);
	}
}
