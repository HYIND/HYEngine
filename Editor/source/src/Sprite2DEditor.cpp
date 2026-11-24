#include "Sprite2DEditor.h"

#include "EditorApp.h"
#include "SceneEditor.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "Project/AssetObject.h"
#include "ECSCore/World.h"

#include <chrono>
#include <filesystem>

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

	Render2D::SpriteFrame GetAssetThumbnail(ProjectManager* projectManager, const AssetMeta& meta)
	{
		if (!projectManager)
			return {};
		if (meta.type != AssetType::Sprite && meta.type != AssetType::AnimatedImage && meta.type != AssetType::Texture)
			return {};

		auto asset = projectManager->LoadAsset(meta.guid);
		if (!asset)
			return {};

		if (meta.type == AssetType::AnimatedImage)
		{
			auto image = asset->AsAnimatedImage();
			return image ? image->Evaluate(EditorTimeMs(), 0) : Render2D::SpriteFrame{};
		}

		auto sprite = asset->AsSprite();
		return sprite ? sprite->Resolve() : Render2D::SpriteFrame{};
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
		ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
		const std::string title = std::format("精灵编辑器 - {}###SpriteEditor", meta->name);
		if (!ImGui::Begin(title.c_str(), &open))
		{
			ImGui::End();
			if (!open) { RestoreSpriteOrigin(projectManager); s_sprite.open = false; }
			return;
		}

		auto regions = asset->GetRegions();
		bool regionsChanged = false;

		// ---- 图片和导入配置 ----
		auto sprite = asset->AsSprite();
		auto texture = sprite ? sprite->GetTexture() : nullptr;
		const glm::vec2 texSize = texture ? glm::vec2(texture->GetSize()) : glm::vec2(0.0f);
		ImGui::Text("图片：%s", asset->GetPath().c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("%.0f x %.0f", texSize.x, texSize.y);

		int filter = s_sprite.config.magFilter == vk::Filter::eNearest ? 0 : 1;
		const char* filters[] = { "NEAREST", "LINEAR" };
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("过滤方式", &filter, filters, IM_ARRAYSIZE(filters)))
		{
			s_sprite.config.minFilter = filter == 0 ? vk::Filter::eNearest : vk::Filter::eLinear;
			s_sprite.config.magFilter = s_sprite.config.minFilter;
		}
		ImGui::SameLine();
		ImGui::Checkbox("Gamma 校正（sRGB）", &s_sprite.config.gammaCorrection);
		ImGui::SameLine();
		const TextureConfig savedConfig = asset->GetConfig();
		const bool configDirty = savedConfig.magFilter != s_sprite.config.magFilter || savedConfig.minFilter != s_sprite.config.minFilter
			|| savedConfig.gammaCorrection != s_sprite.config.gammaCorrection;
		if (!configDirty) ImGui::BeginDisabled();
		if (ImGui::Button("应用导入配置"))
		{
			const std::string source = projectManager->GetProjectAssetFullPath(asset->GetPath());
			if (asset->SetConfig(s_sprite.config, source) && projectManager->SaveAsset(s_sprite.guid))
			{
				s_sprite.originRegions = asset->GetRegions();
				s_sprite.message = "导入配置已应用并保存";
			}
			else
				s_sprite.message = "导入配置应用失败";
		}
		if (!configDirty) ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("按新配置重新加载图片并保存（连同当前的区域一起保存）");

		ImGui::Separator();

		// ---- 左栏：区域列表与属性 ----
		ImGui::BeginChild("##SpriteRegions", ImVec2(300.0f, 0.0f), ImGuiChildFlags_Borders);
		{
			if (ImGui::Button("添加区域"))
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
			if (!canRemove) ImGui::BeginDisabled();
			if (ImGui::Button("删除"))
			{
				regions.erase(regions.begin() + s_sprite.selected);
				s_sprite.selected = std::min(s_sprite.selected, static_cast<int>(regions.size()) - 1);
				regionsChanged = true;
			}
			if (!canRemove) ImGui::EndDisabled();

			// 网格切分：按行列均分整张贴图（常用于图集 / 序列帧）
			ImGui::SetNextItemWidth(120.0f);
			ImGui::DragInt2("行列（列 x 行）", &s_sprite.gridCols, 0.1f, 1, 256);
			s_sprite.gridCols = std::max(1, s_sprite.gridCols);
			s_sprite.gridRows = std::max(1, s_sprite.gridRows);
			if (ImGui::Button("按网格切分（覆盖现有区域）") && texSize.x > 0.0f && texSize.y > 0.0f)
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

			ImGui::Separator();
			for (int i = 0; i < static_cast<int>(regions.size()); ++i)
			{
				ImGui::PushID(i);
				const std::string label = regions[i].name.empty() ? std::format("<未命名 {}>", i) : regions[i].name;
				if (ImGui::Selectable(label.c_str(), s_sprite.selected == i))
					s_sprite.selected = i;
				ImGui::PopID();
			}
			if (regions.empty())
				ImGui::TextDisabled("没有区域：整张图片就是一帧");

			if (s_sprite.selected >= 0 && s_sprite.selected < static_cast<int>(regions.size()))
			{
				auto& region = regions[s_sprite.selected];
				ImGui::Separator();
				ImGui::PushID("selected");
				char nameBuffer[128]{};
				strncpy_s(nameBuffer, region.name.c_str(), _TRUNCATE);
				if (ImGui::InputText("名称", nameBuffer, sizeof(nameBuffer)))
				{
					region.name = nameBuffer;
					regionsChanged = true;
				}
				regionsChanged |= ImGui::DragFloat4("矩形（x,y,宽,高）", &region.rect.x, 1.0f, 0.0f, std::max(texSize.x, texSize.y));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("图片上的像素坐标，原点在左上角");
				regionsChanged |= ImGui::DragFloat2("轴心", &region.pivot.x, 0.01f, 0.0f, 1.0f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("按比例：(0,0) 左上，(0.5,0.5) 中心，(0.5,1) 底边中点。\n实体的位置就是轴心所在的点，旋转和缩放也绕它进行");
				regionsChanged |= ImGui::DragFloat4("九宫格边距（左,上,右,下）", &region.border.x, 0.5f, 0.0f, std::max(region.rect.z, region.rect.w));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("组件绘制模式选九宫格时生效：四角保持原尺寸，四边单向拉伸，中间双向拉伸");
				if (ImGui::Button("整张图片"))
				{
					region.rect = glm::vec4(0.0f, 0.0f, texSize.x, texSize.y);
					regionsChanged = true;
				}
				ImGui::Checkbox("在图上拖拽框选", &s_sprite.dragDefine);

				Render2D::SpriteFrame preview = Render2D::Sprite::MakeFrame(texture, &region);
				Sprite2DEditor::DrawFramePreview(preview, ImVec2(128.0f, 128.0f));
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();

		// ---- 右栏：贴图和区域框 ----
		ImGui::BeginChild("##SpriteCanvas", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
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

		// ---- 底栏 ----
		ImGui::SetNextItemWidth(160.0f);
		float zoomValue = s_sprite.zoom;
		if (ImGui::SliderFloat("缩放（0 = 适配）", &zoomValue, 0.0f, 16.0f, "%.2f"))
			s_sprite.zoom = zoomValue;
		ImGui::SameLine();
		if (ImGui::Button("保存"))
		{
			if (projectManager->SaveAsset(s_sprite.guid))
			{
				s_sprite.originRegions = asset->GetRegions();
				s_sprite.message = "已保存";
			}
			else
				s_sprite.message = "保存失败";
		}
		ImGui::SameLine();
		if (ImGui::Button("还原"))
		{
			RestoreSpriteOrigin(projectManager);
			s_sprite.message = "已还原到上次保存";
		}
		if (!s_sprite.message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", s_sprite.message.c_str());
		}

		// 改动立即作用到运行时对象：场景里引用这个 Sprite 的实体马上能看到
		if (regionsChanged)
		{
			asset->SetRegions(regions);
			s_sprite.message.clear();
		}

		ImGui::End();

		if (!open)
		{
			RestoreSpriteOrigin(projectManager);
			s_sprite.open = false;
		}
	}

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
		ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
		const std::string title = std::format("动图编辑器 - {}###AnimatedImageViewer", meta->name);
		if (ImGui::Begin(title.c_str(), &open))
		{
			auto image = asset->AsAnimatedImage();
			if (!image || image->GetFrameCount() == 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "动图加载失败（看控制台输出）");
			}
			else
			{
				const uint32_t frameCount = image->GetFrameCount();
				ImGui::Text("%u 帧，%u x %u，一轮 %.0f ms，图集 %zu 页", frameCount, image->GetFrameSize().x, image->GetFrameSize().y,
					image->GetTotalDuration(), image->GetPages().size());
				ImGui::TextDisabled("这里只是预览；循环次数、速度在 AnimatedImagePlayer 组件 / UI 节点上设置");

				if (s_viewer.playing)
				{
					s_viewer.timeMs += ImGui::GetIO().DeltaTime * 1000.0f * s_viewer.speed;
					if (image->GetTotalDuration() > 0.0f)
						s_viewer.timeMs = std::fmod(std::max(0.0f, s_viewer.timeMs), image->GetTotalDuration());
					s_viewer.frame = static_cast<int>(image->FrameIndexAt(s_viewer.timeMs, 0));
				}

				ImGui::Checkbox("播放", &s_viewer.playing);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f);
				ImGui::SliderFloat("预览速度", &s_viewer.speed, 0.1f, 4.0f, "%.2fx");
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::SliderInt("##frame", &s_viewer.frame, 0, static_cast<int>(frameCount) - 1, "第 %d 帧"))
				{
					s_viewer.playing = false;
					float t = 0.0f;
					for (int i = 0; i < s_viewer.frame; ++i)
						t += image->GetFrame(static_cast<uint32_t>(i)).durationMs;
					s_viewer.timeMs = t;
				}
				ImGui::Text("本帧 %.0f ms", image->GetFrame(static_cast<uint32_t>(s_viewer.frame)).durationMs);

				const ImVec2 avail = ImGui::GetContentRegionAvail();
				const float side = std::max(64.0f, std::min(avail.x, avail.y - ImGui::GetFrameHeightWithSpacing() * 5.0f));
				Sprite2DEditor::DrawFramePreview(image->GetFrameSprite(static_cast<uint32_t>(s_viewer.frame)), ImVec2(side, side));
			}

			ImGui::Separator();
			ImGui::TextUnformatted("导入配置（改完点应用：重新解码并保存）");
			int filter = s_viewer.config.magFilter == vk::Filter::eNearest ? 0 : 1;
			const char* filters[] = { "NEAREST", "LINEAR" };
			if (ImGui::Combo("过滤方式", &filter, filters, IM_ARRAYSIZE(filters)))
			{
				s_viewer.config.minFilter = filter == 0 ? vk::Filter::eNearest : vk::Filter::eLinear;
				s_viewer.config.magFilter = s_viewer.config.minFilter;
			}
			ImGui::Checkbox("Gamma 校正（sRGB）", &s_viewer.config.gammaCorrection);
			if (ImGui::Button("应用"))
			{
				const std::string source = projectManager->GetProjectAssetFullPath(asset->GetDescription().GetPath());
				if (asset->SetConfig(s_viewer.config, source) && projectManager->SaveAsset(s_viewer.guid))
					s_viewer.message = "已应用并保存（场景里的播放器下次解析时换成新图集）";
				else
					s_viewer.message = "应用失败";
			}
			if (!s_viewer.message.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", s_viewer.message.c_str());
			}
		}
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
