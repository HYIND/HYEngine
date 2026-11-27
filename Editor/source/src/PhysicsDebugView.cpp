#include "PhysicsDebugView.h"
#include "EditorApp.h"
#include "ECSCore/World.h"
#include "Systems/PhysicsSystem.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	PhysicsDebugView::Mode s_mode = PhysicsDebugView::Mode::Off;
	bool s_showAabb = false;
	bool s_showContacts = true;

	// 属性面板悬停的形状（只对下一次 Draw 有效）
	struct HoveredShape
	{
		bool valid = false;
		EntityID entity = 0;
		int child = -1;
	};
	HoveredShape s_hovered;
	constexpr ImU32 kHoverColor = IM_COL32(255, 255, 255, 255);

	// 「全部」模式只画这个距离内的物体（米）
	constexpr float kAllModeMaxDistance = 150.0f;
	// 三角网格只画相机周围这个半径内的三角形（米）
	constexpr float kTriangleMeshRadius = 60.0f;
	// 线段上限：ImDrawList 每条线 4 个顶点 + 6 个索引，6 万条大约 24 万顶点，界面线程还能接受
	constexpr size_t kMaxLines = 60000;

	// 场景线程收集、界面线程读：整份结果用 shared_ptr 交换，界面拿到就不再加锁
	struct LineCache
	{
		std::mutex mutex;
		World* world = nullptr;
		PhysicsDebugView::Mode mode = PhysicsDebugView::Mode::Off;	// 这份结果按哪个模式收集的（悬停预览按「选中」收集）
		std::shared_ptr<const PhysicsDebugLines> lines;
	};
	LineCache s_cache;
	// 上一次收集请求；还没执行完就不再提交
	std::future<void> s_collect;

	ImU32 ToColor(float r, float g, float b, float a)
	{
		return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), static_cast<int>(a * 255.0f));
	}

	// 物体类型的颜色（highlight：选中的更亮、不透明）
	ImU32 ObjectColor(const PhysicsDebugObject& object, bool highlight)
	{
		float r = 0.55f, g = 0.75f, b = 0.55f;		// 静态：灰绿
		if (object.isCharacter)
			r = 0.30f, g = 0.85f, b = 1.00f;		// 角色：青
		else if (object.isSensor)
			r = 0.78f, g = 0.48f, b = 1.00f;		// 传感器：紫
		else if (object.bodyType == Physics::BodyType::Dynamic)
			r = 0.30f, g = 0.95f, b = 0.40f;		// 动态：绿
		else if (object.bodyType == Physics::BodyType::Kinematic)
			r = 1.00f, g = 0.85f, b = 0.25f;		// 运动学：黄

		float alpha = object.isSensor ? 0.55f : 0.80f;
		if (object.sleeping && !object.isCharacter)
		{
			r *= 0.6f; g *= 0.6f; b *= 0.6f;		// 休眠：暗一些
		}
		if (highlight)
		{
			r = std::min(1.0f, r * 1.25f + 0.1f);
			g = std::min(1.0f, g * 1.25f + 0.1f);
			b = std::min(1.0f, b * 1.25f + 0.1f);
			alpha = 1.0f;
		}
		return ToColor(r, g, b, alpha);
	}

	constexpr ImU32 kAabbColor = IM_COL32(255, 90, 80, 150);
	constexpr ImU32 kContactColor = IM_COL32(255, 240, 60, 255);
}

namespace PhysicsDebugView
{
	Mode GetMode() { return s_mode; }
	void SetMode(Mode mode) { s_mode = mode; }

	void SetHoveredShape(Entity entity, int childIndex)
	{
		if (!entity || !entity.isValid())
			return;
		s_hovered = { true, entity.getId(), childIndex };
	}

	void DrawToolbar()
	{
		int mode = static_cast<int>(s_mode);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
		if (ImGui::Combo("碰撞体", &mode, "关\0选中\0全部\0"))
			s_mode = static_cast<Mode>(std::clamp(mode, 0, 2));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("显示物理碰撞形状（Bullet 里实际参与碰撞的形状）\n"
				"选中：只画选中的实体；全部：画 %.0f 米内、视野内的实体\n"
				"三角网格只画相机周围 %.0f 米内的三角形；线段太多时会截断",
				kAllModeMaxDistance, kTriangleMeshRadius);
		if (s_mode == Mode::Off)
			return;
		ImGui::SameLine();
		ImGui::Checkbox("AABB", &s_showAabb);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("同时画 Bullet 的世界 AABB（宽相检测用的包围盒，红色）");
		ImGui::SameLine();
		ImGui::Checkbox("接触点", &s_showContacts);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("画接触点和法线（黄色）；只有播放时才有，编辑时不做物理模拟");
	}

	void Draw(EditorApp* app, const std::vector<Entity>& selection, const glm::mat4& viewProjGL, const glm::vec3& cameraPos,
		const ImVec2& viewportPos, const ImVec2& viewportSize)
	{
		// 悬停只对这一次 Draw 有效：先取出来再清掉
		const HoveredShape hovered = s_hovered;
		s_hovered = {};

		World* world = app ? app->GetWorld() : nullptr;
		// 显示模式是「关」但属性面板悬停在某个形状上：临时按「选中」画这个实体
		const bool hoverPreview = s_mode == Mode::Off && hovered.valid;
		const Mode mode = hoverPreview ? Mode::Selected : s_mode;
		if (mode == Mode::Off || !world || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		std::vector<EntityID> selectedIds;
		selectedIds.reserve(selection.size());
		for (const Entity& e : selection)
		{
			if (e && e.isValid())
				selectedIds.push_back(e.getId());
		}
		std::sort(selectedIds.begin(), selectedIds.end());
		selectedIds.erase(std::unique(selectedIds.begin(), selectedIds.end()), selectedIds.end());
		// 收集用的实体：悬停预览只画悬停的那个
		const std::vector<EntityID> filterIds = hoverPreview ? std::vector<EntityID>{ hovered.entity } : selectedIds;

		// 请求下一份结果（上一次还没执行完就不提交，界面继续用旧的）
		using namespace std::chrono_literals;
		if (!s_collect.valid() || s_collect.wait_for(0s) == std::future_status::ready)
		{
			PhysicsDebugDrawOptions options;
			options.cameraPosition = cameraPos;
			options.triangleMeshRadius = kTriangleMeshRadius;
			options.maxLines = kMaxLines;
			options.drawAabb = s_showAabb;
			options.drawContacts = s_showContacts;
			if (mode == Mode::Selected)
			{
				options.filterByEntities = true;
				options.onlyEntities = filterIds;
			}
			else
			{
				options.maxDistance = kAllModeMaxDistance;
				options.useFrustum = true;
				options.viewProj = viewProjGL;
			}

			if (mode == Mode::Selected && filterIds.empty())
			{
				std::lock_guard lock(s_cache.mutex);
				s_cache.world = world;
				s_cache.mode = mode;
				s_cache.lines.reset();
			}
			else
			{
				s_collect = world->SubmitCommand([options = std::move(options), mode](World& w)
					{
						auto lines = std::make_shared<PhysicsDebugLines>();
						if (auto* physics = w.getSystem<PhysicsSystem>())
							physics->CollectDebugLines(options, *lines);
						std::lock_guard lock(s_cache.mutex);
						s_cache.world = &w;
						s_cache.mode = mode;
						s_cache.lines = std::move(lines);
					});
			}
		}

		std::shared_ptr<const PhysicsDebugLines> data;
		{
			std::lock_guard lock(s_cache.mutex);
			// 换场景 / 切换模式后第一份结果还没到：不画旧的
			if (s_cache.world != world || s_cache.mode != mode)
				return;
			data = s_cache.lines;
		}
		if (!data || data->lines.empty())
			return;

		// 每个物体的颜色 / 线宽算一次
		struct ObjectStyle
		{
			ImU32 color;
			float thickness;
		};
		std::vector<ObjectStyle> styles;
		styles.reserve(data->objects.size());
		for (const auto& object : data->objects)
		{
			const bool highlight = mode == Mode::All
				&& std::binary_search(selectedIds.begin(), selectedIds.end(), object.entity.getId());
			styles.push_back({ ObjectColor(object, highlight), highlight ? 2.0f : 1.0f });
		}

		const ImVec2 vpMax(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
		auto toScreen = [&](const glm::vec4& clip)
			{
				const float invW = 1.0f / clip.w;
				return ImVec2(viewportPos.x + (clip.x * invW * 0.5f + 0.5f) * viewportSize.x,
					viewportPos.y + (0.5f - clip.y * invW * 0.5f) * viewportSize.y);
			};

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(viewportPos, vpMax, true);
		constexpr float eps = 1e-3f;
		size_t drawn = 0;
		for (const auto& line : data->lines)
		{
			glm::vec4 a = viewProjGL * glm::vec4(line.a, 1.0f);
			glm::vec4 b = viewProjGL * glm::vec4(line.b, 1.0f);
			// 两端都在相机后面，或者都在视锥同一侧外面：不画
			if (a.w <= eps && b.w <= eps)
				continue;
			if ((a.x < -a.w && b.x < -b.w) || (a.x > a.w && b.x > b.w) || (a.y < -a.w && b.y < -b.w) || (a.y > a.w && b.y > b.w))
				continue;
			// 一端在相机后面：在裁剪空间截到 w = eps
			if (a.w <= eps)
				a = glm::mix(a, b, (eps - a.w) / (b.w - a.w));
			else if (b.w <= eps)
				b = glm::mix(b, a, (eps - b.w) / (a.w - b.w));

			ImU32 color = kContactColor;
			float thickness = 1.5f;
			if (line.kind == PhysicsDebugLineKind::Aabb)
			{
				color = kAabbColor;
				thickness = 1.0f;
			}
			else if (line.kind == PhysicsDebugLineKind::Shape && line.object < styles.size())
			{
				color = styles[line.object].color;
				thickness = styles[line.object].thickness;
				// 属性面板悬停的那个形状：白色、加粗
				if (hovered.valid && line.child == hovered.child && data->objects[line.object].entity.getId() == hovered.entity)
				{
					color = kHoverColor;
					thickness = 2.5f;
				}
			}
			drawList->AddLine(toScreen(a), toScreen(b), color, thickness);
			++drawn;
		}

		// 左下角：数量统计；截断时提示（悬停预览时不显示）
		if (hoverPreview)
		{
			drawList->PopClipRect();
			return;
		}
		const std::string info = data->truncated
			? std::format("碰撞体：{} 个，{} 条线（已达上限 {}，部分没有画）", data->objects.size(), data->lines.size(), kMaxLines)
			: std::format("碰撞体：{} 个，{} 条线", data->objects.size(), drawn);
		const ImVec2 textPos(viewportPos.x + 10.0f, vpMax.y - ImGui::GetTextLineHeight() - 8.0f);
		drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 200), info.c_str());
		drawList->AddText(textPos, data->truncated ? IM_COL32(255, 170, 80, 255) : IM_COL32(220, 220, 220, 220), info.c_str());
		drawList->PopClipRect();
	}
}
