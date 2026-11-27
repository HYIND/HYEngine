#include "ViewportIcons.h"
#include "EditorApp.h"
#include "EditorTags.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "ECSCore/World.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <future>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	// Entity::INVALID_ID 是私有的，这里用同一个值表示「没有实体」
	constexpr EntityID kNoEntity = std::numeric_limits<EntityID>::max();

	enum class IconKind : uint8_t
	{
		DirLight, PointLight, SpotLight, Camera, DDGIVolume, Effect, Audio, Empty
	};

	struct IconItem
	{
		EntityID id = kNoEntity;
		glm::vec3 position = glm::vec3(0.0f);
		IconKind kind = IconKind::Empty;
		std::string name;
	};

	// 场景线程收集、界面线程读
	struct IconCache
	{
		std::mutex mutex;
		World* world = nullptr;
		std::vector<IconItem> items;
	};
	IconCache s_cache;
	// 上一次收集请求；还没执行完就不再提交（World 销毁时 future 会变成就绪，不会卡住）
	std::future<void> s_collect;

	// 本帧画出来的图标（界面线程独占）
	struct ScreenIcon
	{
		EntityID id = kNoEntity;
		ImVec2 min, max;
		float depth = 0.0f;			// 到相机的距离，点选取最近的
	};
	std::vector<ScreenIcon> s_screenIcons;
	World* s_screenWorld = nullptr;
	bool s_hovered = false;
	bool s_visible = true;

	// 距离淡出（米）：超过 kFadeEnd 不画、也不能点；选中的始终画
	constexpr float kFadeStart = 60.0f;
	constexpr float kFadeEnd = 150.0f;
	// 太淡的图标不参与点选，免得点到几乎看不见的东西
	constexpr float kPickMinAlpha = 0.25f;

	// 编辑器自己的相机（自由相机实体，以及跟随它的那台相机）不画
	bool IsEditorCamera(Entity entity)
	{
		if (entity.hasComponent<TagEditorCamera>())
			return true;
		if (const auto* follow = entity.tryGetComponent<CameraFollow>())
		{
			const Entity target = follow->target;
			if (target && target.isValid() && target.hasComponent<TagEditorCamera>())
				return true;
		}
		return false;
	}

	// 有网格的、2D 的、屏幕空间的实体有自己的拾取方式，不画图标
	bool Classify(Entity entity, IconKind& kind)
	{
		if (IsEditorCamera(entity))
			return false;
		if (const auto* light = entity.tryGetComponent<RenderLight>())
		{
			switch (light->type)
			{
			case LightType::Point: kind = IconKind::PointLight; break;
			case LightType::Spot: kind = IconKind::SpotLight; break;
			default: kind = IconKind::DirLight; break;
			}
			return true;
		}
		if (entity.hasComponent<CameraComponent>()) { kind = IconKind::Camera; return true; }
		if (entity.hasComponent<DDGIVolumeComponent>()) { kind = IconKind::DDGIVolume; return true; }
		if (entity.hasComponent<ParticleEmitter>() || entity.hasComponent<LaserBeamEmitter>()) { kind = IconKind::Effect; return true; }
		if (entity.hasComponent<AudioSource>()) { kind = IconKind::Audio; return true; }
		if (entity.hasComponent<RenderStaticModel>() || entity.hasComponent<RenderSkinnedModel>()
			|| entity.hasComponent<Sprite>() || entity.hasComponent<AnimatedImagePlayer>()
			|| entity.hasComponent<UIView>() || entity.hasComponent<SkyBox>())
			return false;
		kind = IconKind::Empty;
		return true;
	}

	void Collect(World& world, std::vector<IconItem>& out)
	{
		for (auto [entity, transform] : world.getViewWith<Transform>())
		{
			IconKind kind;
			if (!Classify(entity, kind))
				continue;
			IconItem item;
			item.id = entity.getId();
			item.position = transform.position;
			item.kind = kind;
			if (const auto* name = entity.tryGetComponent<TagEditorName>(); name && !name->name.empty())
				item.name = name->name;
			else
				item.name = std::format("Entity {}", item.id);
			out.push_back(std::move(item));
		}
	}

	ImU32 KindColor(IconKind kind, float alpha)
	{
		const int a = std::clamp(int(alpha * 255.0f + 0.5f), 0, 255);
		switch (kind)
		{
		case IconKind::DirLight:   return IM_COL32(255, 214, 90, a);
		case IconKind::PointLight: return IM_COL32(255, 190, 80, a);
		case IconKind::SpotLight:  return IM_COL32(255, 170, 70, a);
		case IconKind::Camera:     return IM_COL32(120, 190, 255, a);
		case IconKind::DDGIVolume: return IM_COL32(90, 220, 200, a);
		case IconKind::Effect:     return IM_COL32(230, 120, 255, a);
		case IconKind::Audio:      return IM_COL32(120, 230, 120, a);
		default:                   return IM_COL32(205, 205, 205, a);
		}
	}

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		const uint32_t a = uint32_t(std::clamp(alpha, 0.0f, 1.0f) * float((color >> IM_COL32_A_SHIFT) & 0xFF) + 0.5f);
		return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
	}

	// 图标内部的图形，c 为中心、r 为图标半径；坐标按 r 的倍数写
	void DrawGlyph(ImDrawList* dl, IconKind kind, const ImVec2& c, float r, ImU32 col)
	{
		auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
		const float line = std::max(1.0f, r * 0.12f);

		switch (kind)
		{
		case IconKind::DirLight:		// 太阳：圆 + 8 条光线
		{
			dl->AddCircleFilled(c, 0.28f * r, col);
			for (int k = 0; k < 8; ++k)
			{
				const float a = float(k) * 0.78539816f;
				const float cs = std::cos(a), sn = std::sin(a);
				dl->AddLine(P(cs * 0.44f, sn * 0.44f), P(cs * 0.68f, sn * 0.68f), col, line);
			}
			break;
		}
		case IconKind::PointLight:		// 灯泡
			dl->AddCircleFilled(P(0.0f, -0.12f), 0.38f * r, col);
			dl->AddRectFilled(P(-0.17f, 0.24f), P(0.17f, 0.50f), col, 0.06f * r);
			break;
		case IconKind::SpotLight:		// 光锥
		{
			dl->AddTriangleFilled(P(0.0f, -0.42f), P(-0.46f, 0.46f), P(0.46f, 0.46f), WithAlpha(col, 0.45f));
			dl->AddTriangle(P(0.0f, -0.42f), P(-0.46f, 0.46f), P(0.46f, 0.46f), col, line);
			dl->AddCircleFilled(P(0.0f, -0.42f), 0.15f * r, col);
			break;
		}
		case IconKind::Camera:			// 机身 + 镜头
			dl->AddRectFilled(P(-0.52f, -0.30f), P(0.14f, 0.30f), col, 0.08f * r);
			dl->AddTriangleFilled(P(0.14f, 0.0f), P(0.56f, -0.30f), P(0.56f, 0.30f), col);
			break;
		case IconKind::DDGIVolume:		// 3×3 探针点阵
			for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
					dl->AddCircleFilled(P(0.38f * float(x), 0.38f * float(y)), 0.10f * r, col);
			break;
		case IconKind::Effect:			// 四角星（两个细菱形）
		{
			const ImVec2 v[4] = { P(0.0f, -0.62f), P(0.15f, 0.0f), P(0.0f, 0.62f), P(-0.15f, 0.0f) };
			const ImVec2 h[4] = { P(-0.62f, 0.0f), P(0.0f, -0.15f), P(0.62f, 0.0f), P(0.0f, 0.15f) };
			dl->AddConvexPolyFilled(v, 4, col);
			dl->AddConvexPolyFilled(h, 4, col);
			break;
		}
		case IconKind::Audio:			// 喇叭 + 两道声波
		{
			dl->AddRectFilled(P(-0.50f, -0.15f), P(-0.24f, 0.15f), col);
			const ImVec2 cone[4] = { P(-0.24f, -0.15f), P(0.04f, -0.42f), P(0.04f, 0.42f), P(-0.24f, 0.15f) };
			dl->AddConvexPolyFilled(cone, 4, col);
			dl->PathArcTo(P(0.04f, 0.0f), 0.24f * r, -0.9f, 0.9f);
			dl->PathStroke(col, 0, line);
			dl->PathArcTo(P(0.04f, 0.0f), 0.44f * r, -0.9f, 0.9f);
			dl->PathStroke(col, 0, line);
			break;
		}
		default:						// 空实体：坐标轴十字
			dl->AddLine(P(-0.46f, 0.0f), P(0.46f, 0.0f), col, line);
			dl->AddLine(P(0.0f, -0.46f), P(0.0f, 0.46f), col, line);
			dl->AddCircle(c, 0.16f * r, col, 0, line);
			break;
		}
	}
}

namespace ViewportIcons
{
	bool& Visible() { return s_visible; }

	bool IsHovered() { return s_hovered; }

	void Clear()
	{
		s_screenIcons.clear();
		s_screenWorld = nullptr;
		s_hovered = false;
	}

	void Draw(EditorApp* app, const std::vector<Entity>& selected, const glm::mat4& viewProjGL, const glm::vec3& cameraPos,
		const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		Clear();
		World* world = app ? app->GetWorld() : nullptr;
		if (!s_visible || !world || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		// 请求下一份收集结果
		using namespace std::chrono_literals;
		if (!s_collect.valid() || s_collect.wait_for(0s) == std::future_status::ready)
		{
			s_collect = world->SubmitCommand([](World& w)
				{
					std::vector<IconItem> items;
					Collect(w, items);
					std::lock_guard lock(s_cache.mutex);
					s_cache.world = &w;
					s_cache.items = std::move(items);
				});
		}

		std::vector<IconItem> items;
		{
			std::lock_guard lock(s_cache.mutex);
			if (s_cache.world != world)
				return;			// 换场景后第一份结果还没到
			items = s_cache.items;
		}
		if (items.empty())
			return;

		const float r = std::max(8.0f, ImGui::GetFontSize() * 0.8f);
		const ImVec2 vpMax(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
		std::unordered_set<EntityID> selectedIds;
		for (const Entity& e : selected)
		{
			if (e && e.isValid())
				selectedIds.insert(e.getId());
		}
		auto isSelectedId = [&](EntityID id) { return selectedIds.contains(id); };

		struct VisibleIcon
		{
			const IconItem* item;
			ImVec2 center;
			float depth;
			float alpha;
		};
		std::vector<VisibleIcon> visible;
		visible.reserve(items.size());
		for (const auto& item : items)
		{
			const glm::vec4 clip = viewProjGL * glm::vec4(item.position, 1.0f);
			if (clip.w <= 0.05f)
				continue;		// 相机后面 / 贴着相机
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			const ImVec2 center(viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
				viewportPos.y + (0.5f - ndc.y * 0.5f) * viewportSize.y);
			if (center.x < viewportPos.x - r || center.y < viewportPos.y - r || center.x > vpMax.x + r || center.y > vpMax.y + r)
				continue;

			const float depth = glm::length(item.position - cameraPos);
			float alpha = 1.0f - std::clamp((depth - kFadeStart) / (kFadeEnd - kFadeStart), 0.0f, 1.0f);
			if (isSelectedId(item.id))
				alpha = 1.0f;
			if (alpha <= 0.02f)
				continue;
			visible.push_back({ &item, center, depth, alpha });
		}
		if (visible.empty())
			return;

		// 从远到近画，近的盖在上面
		std::sort(visible.begin(), visible.end(), [](const VisibleIcon& a, const VisibleIcon& b) { return a.depth > b.depth; });

		// 先算点选矩形和悬停（悬停的那个画高亮）
		const ImVec2 mouse = ImGui::GetMousePos();
		// 被别的窗口挡住时不算悬停（不高亮、不出提示）
		const bool mouseInViewport = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
			&& mouse.x >= viewportPos.x && mouse.y >= viewportPos.y && mouse.x < vpMax.x && mouse.y < vpMax.y;
		EntityID hoveredId = kNoEntity;
		float hoveredDepth = 0.0f;
		const IconItem* hoveredItem = nullptr;
		s_screenIcons.reserve(visible.size());
		for (const auto& v : visible)
		{
			if (v.alpha < kPickMinAlpha)
				continue;
			ScreenIcon icon;
			icon.id = v.item->id;
			icon.min = ImVec2(v.center.x - r, v.center.y - r);
			icon.max = ImVec2(v.center.x + r, v.center.y + r);
			icon.depth = v.depth;
			s_screenIcons.push_back(icon);

			if (mouseInViewport && mouse.x >= icon.min.x && mouse.y >= icon.min.y && mouse.x <= icon.max.x && mouse.y <= icon.max.y
				&& (!hoveredItem || v.depth < hoveredDepth))
			{
				hoveredId = icon.id;
				hoveredDepth = v.depth;
				hoveredItem = v.item;
			}
		}
		s_screenWorld = world;
		s_hovered = hoveredItem != nullptr;

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->PushClipRect(viewportPos, vpMax, true);
		for (const auto& v : visible)
		{
			const bool isSelected = isSelectedId(v.item->id);
			const bool isHovered = v.item->id == hoveredId;
			const ImU32 color = KindColor(v.item->kind, v.alpha);

			dl->AddCircleFilled(v.center, r, IM_COL32(18, 18, 20, int(190.0f * v.alpha)));
			if (isSelected)
			{
				dl->AddCircle(v.center, r + 2.5f, IM_COL32(0, 0, 0, 160), 0, 3.0f);
				dl->AddCircle(v.center, r + 2.5f, IM_COL32(255, 160, 40, 255), 0, 2.0f);
			}
			dl->AddCircle(v.center, r, isHovered ? IM_COL32(255, 255, 255, int(230.0f * v.alpha)) : color, 0, isHovered ? 2.0f : 1.25f);
			DrawGlyph(dl, v.item->kind, v.center, r, isHovered ? WithAlpha(IM_COL32(255, 255, 255, 255), v.alpha) : color);
		}
		dl->PopClipRect();

		if (hoveredItem && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
			ImGui::SetTooltip("%s", hoveredItem->name.c_str());
	}

	Entity Pick(EditorApp* app, const ImVec2& mouse)
	{
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || world != s_screenWorld)
			return Entity();

		const ScreenIcon* best = nullptr;
		for (const auto& icon : s_screenIcons)
		{
			if (mouse.x < icon.min.x || mouse.y < icon.min.y || mouse.x > icon.max.x || mouse.y > icon.max.y)
				continue;
			if (!best || icon.depth < best->depth)
				best = &icon;
		}
		if (!best)
			return Entity();

		const Entity entity(world, best->id);
		return world->IsValidWorldEntity(entity) ? entity : Entity();
	}

	std::vector<Entity> PickRect(EditorApp* app, const ImVec2& min, const ImVec2& max)
	{
		std::vector<Entity> result;
		World* world = app ? app->GetWorld() : nullptr;
		if (!world || world != s_screenWorld)
			return result;

		const ImVec2 lo(std::min(min.x, max.x), std::min(min.y, max.y));
		const ImVec2 hi(std::max(min.x, max.x), std::max(min.y, max.y));
		for (const auto& icon : s_screenIcons)
		{
			const ImVec2 c((icon.min.x + icon.max.x) * 0.5f, (icon.min.y + icon.max.y) * 0.5f);
			if (c.x < lo.x || c.y < lo.y || c.x > hi.x || c.y > hi.y)
				continue;
			const Entity entity(world, icon.id);
			if (world->IsValidWorldEntity(entity))
				result.push_back(entity);
		}
		return result;
	}
}
