#include "PhysicsDebugView.h"
#include "EditorApp.h"
#include "ECSCore/World.h"
#include "Systems/PhysicsSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
	// 线段数不设上限：距离范围内的都画

	// 每个物体的颜色 / 线宽；投影后的屏幕线段。跨帧复用，只增不减容量
	struct ObjectStyle
	{
		ImU32 color;
		float thickness;
	};
	struct ScreenLine
	{
		ImVec2 a, b;
		ImU32 color;
		float thickness;
	};
	std::vector<ObjectStyle> s_styles;
	std::vector<ScreenLine> s_screenLines;		// 前 s_objectLineCount 条是物体的形状线，后面是 AABB / 接触点

	// 逐顶点的投影结果（跨帧复用）：裁剪空间坐标、区域码、屏幕坐标
	// 区域码：每一位表示在视锥的某个面外面（左、右、下、上、相机后面）。两个端点的区域码按位与不为 0，
	// 说明两端在同一个面外面，整条边都看不见（裁剪空间里这些面都是线性的半空间，相机后面的点也成立）
	enum : uint8_t { kOutLeft = 1, kOutRight = 2, kOutBottom = 4, kOutTop = 8, kOutBehind = 16 };
	std::vector<glm::vec4> s_clip;
	std::vector<uint8_t> s_code;
	std::vector<ImVec2> s_screen;

	// 上一帧物体形状线的投影输入：全部一样（相机、物体都没动，颜色 / 悬停没变）就直接复用 s_screenLines 的前一段
	struct ProjectionKey
	{
		const PhysicsDebugShapeLines* lines = nullptr;
		glm::mat4 mvp{ 1.0f };
		ImU32 color = 0;
		float thickness = 0.0f;
		int hoverChild = -1;
		bool operator==(const ProjectionKey& o) const
		{
			return lines == o.lines && mvp == o.mvp && color == o.color && thickness == o.thickness && hoverChild == o.hoverChild;
		}
	};
	std::vector<ProjectionKey> s_lastKeys, s_keys;
	ImVec2 s_lastViewportPos{ 0.0f, 0.0f }, s_lastViewportSize{ 0.0f, 0.0f };
	size_t s_objectLineCount = 0;
	bool s_projectionValid = false;

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
				"三角网格只画相机周围 %.0f 米内的三角形",
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
		if (!data || (data->objects.empty() && data->lines.empty()))
			return;

		// 分项计时（定位绘制耗时用）：准备 → 投影 → 写顶点
		using Clock = std::chrono::steady_clock;
		const auto msSince = [](Clock::time_point t) { return std::chrono::duration<float, std::milli>(Clock::now() - t).count(); };

		// 每个物体的颜色 / 线宽、投影输入算一次（数组跨帧复用，不每帧分配）
		const auto drawStart = Clock::now();
		s_styles.clear();
		s_styles.reserve(data->objects.size());
		s_keys.clear();
		s_keys.reserve(data->objects.size());
		for (const auto& object : data->objects)
		{
			const bool highlight = mode == Mode::All
				&& std::binary_search(selectedIds.begin(), selectedIds.end(), object.entity.getId());
			const ObjectStyle style{ ObjectColor(object, highlight), highlight ? 2.0f : 1.0f };
			s_styles.push_back(style);
			const bool hoverObject = hovered.valid && object.entity.getId() == hovered.entity;
			s_keys.push_back({ object.lines.get(), viewProjGL * object.model, style.color, style.thickness, hoverObject ? hovered.child : -1 });
		}

		// ---- 第一遍：投影 ----
		const ImVec2 vpMax(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
		const float halfW = viewportSize.x * 0.5f;
		const float halfH = viewportSize.y * 0.5f;
		const float centerX = viewportPos.x + halfW;
		const float centerY = viewportPos.y + halfH;
		constexpr float eps = 1e-3f;
		const float prepareMs = msSince(drawStart);
		const auto projectStart = Clock::now();

		const auto toScreen = [&](const glm::vec4& c)
			{
				const float iw = 1.0f / c.w;
				return ImVec2(centerX + c.x * iw * halfW, centerY - c.y * iw * halfH);
			};
		const auto outCode = [&](const glm::vec4& c) -> uint8_t
			{
				return static_cast<uint8_t>((c.x < -c.w ? kOutLeft : 0) | (c.x > c.w ? kOutRight : 0)
					| (c.y < -c.w ? kOutBottom : 0) | (c.y > c.w ? kOutTop : 0) | (c.w <= eps ? kOutBehind : 0));
			};
		// 一端在相机后面的边：在裁剪空间截到 w = eps 再投影
		const auto clipNear = [&](glm::vec4 a, glm::vec4 b, ImVec2& sa, ImVec2& sb)
			{
				if (a.w <= eps)
					a = glm::mix(a, b, (eps - a.w) / (b.w - a.w));
				else
					b = glm::mix(b, a, (eps - b.w) / (a.w - b.w));
				sa = toScreen(a);
				sb = toScreen(b);
			};

		size_t processedVertices = 0, processedEdges = 0, culledBehind = 0, culledOutside = 0, clippedNear = 0;
		const bool reuse = s_projectionValid && s_keys == s_lastKeys
			&& viewportPos.x == s_lastViewportPos.x && viewportPos.y == s_lastViewportPos.y
			&& viewportSize.x == s_lastViewportSize.x && viewportSize.y == s_lastViewportSize.y;
		if (reuse)
		{
			// 物体形状线和上一帧一样：保留前一段，只重算后面的 AABB / 接触点
			s_screenLines.resize(s_objectLineCount);
		}
		else
		{
			s_screenLines.clear();
			s_screenLines.reserve(data->shapeLineCount + data->lines.size());
			for (size_t o = 0; o < data->objects.size(); ++o)
			{
				const auto& object = data->objects[o];
				if (!object.lines)
					continue;
				const auto& vertices = object.lines->vertices;
				const auto& edges = object.lines->edges;
				const ProjectionKey& key = s_keys[o];
				const glm::mat4& mvp = key.mvp;

				// 每个顶点只变换一次：裁剪空间坐标、区域码；不在相机后面的顺便算好屏幕坐标
				const size_t n = vertices.size();
				s_clip.resize(n);
				s_code.resize(n);
				s_screen.resize(n);
				for (size_t i = 0; i < n; ++i)
				{
					const glm::vec4 c = mvp * glm::vec4(vertices[i], 1.0f);
					const uint8_t code = outCode(c);
					s_clip[i] = c;
					s_code[i] = code;
					if (!(code & kOutBehind))
						s_screen[i] = toScreen(c);
				}
				processedVertices += n;
				processedEdges += edges.size();

				// 每条边：区域码按位与不为 0 就整条看不见；一端在相机后面才需要截近平面
				for (const auto& edge : edges)
				{
					const uint8_t ca = s_code[edge.a];
					const uint8_t cb = s_code[edge.b];
					if (ca & cb)
					{
						++((ca & cb & kOutBehind) ? culledBehind : culledOutside);
						continue;
					}
					// 属性面板悬停的那个形状：白色、加粗
					const bool highlight = key.hoverChild >= 0 && static_cast<int>(edge.child) == key.hoverChild;
					const ImU32 color = highlight ? kHoverColor : key.color;
					const float thickness = highlight ? 2.5f : key.thickness;
					if ((ca | cb) & kOutBehind)
					{
						++clippedNear;
						ImVec2 sa, sb;
						clipNear(s_clip[edge.a], s_clip[edge.b], sa, sb);
						s_screenLines.push_back({ sa, sb, color, thickness });
					}
					else
						s_screenLines.push_back({ s_screen[edge.a], s_screen[edge.b], color, thickness });
				}
			}
			s_objectLineCount = s_screenLines.size();
			s_lastKeys.swap(s_keys);
			s_lastViewportPos = viewportPos;
			s_lastViewportSize = viewportSize;
			s_projectionValid = true;
		}

		// AABB / 接触点：数量少，每帧按世界坐标现算
		for (const auto& line : data->lines)
		{
			const glm::vec4 a = viewProjGL * glm::vec4(line.a, 1.0f);
			const glm::vec4 b = viewProjGL * glm::vec4(line.b, 1.0f);
			const uint8_t ca = outCode(a), cb = outCode(b);
			if (ca & cb)
				continue;
			const bool aabb = line.kind == PhysicsDebugLineKind::Aabb;
			ImVec2 sa, sb;
			if ((ca | cb) & kOutBehind)
				clipNear(a, b, sa, sb);
			else
			{
				sa = toScreen(a);
				sb = toScreen(b);
			}
			s_screenLines.push_back({ sa, sb, aabb ? kAabbColor : kContactColor, aabb ? 1.0f : 1.5f });
		}
		const float projectMs = msSince(projectStart);

		// ---- 第二遍：直接写顶点。每条线一个四边形（按线宽沿法线展开），不做抗锯齿；
		//      16 位索引一次 PrimReserve 不能超过 65536 个顶点，分批写（后端支持 VtxOffset，ImGui 自动切换顶点偏移）----
		const auto writeStart = Clock::now();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(viewportPos, vpMax, true);
		const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
		const int vtxBefore = drawList->VtxBuffer.Size;
		const int cmdBefore = drawList->CmdBuffer.Size;
		constexpr size_t kBatch = 16000;
		size_t batches = 0;
		for (size_t first = 0; first < s_screenLines.size(); first += kBatch)
		{
			++batches;
			const size_t count = std::min(kBatch, s_screenLines.size() - first);
			drawList->PrimReserve(static_cast<int>(count * 6), static_cast<int>(count * 4));
			for (size_t k = first; k < first + count; ++k)
			{
				const ScreenLine& line = s_screenLines[k];
				float dx = line.b.x - line.a.x;
				float dy = line.b.y - line.a.y;
				const float lengthSq = dx * dx + dy * dy;
				const float half = line.thickness * 0.5f;
				if (lengthSq > 1e-12f)
				{
					const float scale = half / std::sqrt(lengthSq);
					dx *= scale;
					dy *= scale;
				}
				else
				{
					dx = half;		// 退化成一个点：画一个小方块
					dy = 0.0f;
				}
				// 法线 (-dy, dx)
				drawList->PrimQuadUV(
					ImVec2(line.a.x - dy, line.a.y + dx), ImVec2(line.b.x - dy, line.b.y + dx),
					ImVec2(line.b.x + dy, line.b.y - dx), ImVec2(line.a.x + dy, line.a.y - dx),
					uv, uv, uv, uv, line.color);
			}
		}
		const float writeMs = msSince(writeStart);
		const float drawMs = msSince(drawStart);
		const int vtxWritten = drawList->VtxBuffer.Size - vtxBefore;
		const int cmdAdded = drawList->CmdBuffer.Size - cmdBefore;

		// 左下角：数量统计和分项耗时；截断时提示（悬停预览时不显示）
		if (hoverPreview)
		{
			drawList->PopClipRect();
			return;
		}
		const std::string line1 = std::format("碰撞体：{} 个，{} 个顶点、{} 条边（画了 {} 条）· 收集 {:.2f} ms（重建 {} 个）· 绘制 {:.2f} ms",
			data->objects.size(), data->shapeVertexCount, data->shapeLineCount + data->lines.size(), s_screenLines.size(),
			data->collectMs, data->rebuiltObjects, drawMs);
		const std::string projectInfo = reuse
			? std::string("复用上一帧")
			: std::format("变换 {} 个顶点，{} 条边：相机后 {}，视锥外 {}，截近平面 {}", processedVertices, processedEdges, culledBehind, culledOutside, clippedNear);
		const std::string line2 = std::format(
			"准备 {:.2f} ms · 投影 {:.2f} ms（{}）· 写顶点 {:.2f} ms（{} 批，{} 顶点，+{} 绘制命令）",
			prepareMs, projectMs, projectInfo, writeMs, batches, vtxWritten, cmdAdded);
		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const ImVec2 textPos(viewportPos.x + 10.0f, vpMax.y - lineHeight * 2.0f - 6.0f);
		const auto shadowText = [&](const ImVec2& p, ImU32 color, const std::string& text)
			{
				drawList->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 200), text.c_str());
				drawList->AddText(p, color, text.c_str());
			};
		shadowText(textPos, IM_COL32(220, 220, 220, 220), line1);
		shadowText(ImVec2(textPos.x, textPos.y + lineHeight), IM_COL32(200, 200, 200, 220), line2);
		drawList->PopClipRect();
	}
}
