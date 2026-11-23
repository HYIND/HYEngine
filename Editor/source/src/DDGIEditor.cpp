#include "DDGIEditor.h"
#include "EditorApp.h"
#include "PropertiesHelper.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "RenderEngine/Renderer3D.h"
#include "VulkanRenderEngine/General/DDGIDebug.h"
#include "VulkanRenderEngine/General/DDGIVolumeDesc.h"
#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	// 每探针常驻显存（图集原地更新，没有 history）：辐照度 10×10×4B、深度 18×18×4B、probeData / probeStats 各 16B
	constexpr double kBytesPerProbe = 10.0 * 10.0 * 4.0 + 18.0 * 18.0 * 4.0 + 32.0;
	constexpr uint32_t kCascadeIdBase = 0xFFFF0000u;
	// 漏光调试记录最多的体积数（同 DDGIAtlasLayout::LeakMaxVolumes）
	constexpr uint32_t DDGIAtlasLayout_LeakVolumes = 4;

	struct EditorState
	{
		DDGIProbeDrawMode drawMode = DDGIProbeDrawMode::Irradiance;
		float probeRadius = 0.12f;
		bool onlySelectedVolume = false;
		bool drawVolumeBoxes = true;

		bool leakDebug = false;
		bool leakFreeze = false;
		bool hovered = false;
		glm::vec2 hoverUV = glm::vec2(0.5f);
		glm::vec2 leakUV = glm::vec2(0.5f);
		bool leakUVValid = false;

		bool probeEditMode = false;
		uint32_t selectedVolumeId = 0;
		std::shared_ptr<const std::vector<glm::ivec3>> selectedCells = std::make_shared<std::vector<glm::ivec3>>();
		uint64_t selectionVersion = 1;
		glm::ivec3 pickLocal = glm::ivec3(0);

		uint64_t pendingBake = 0;
		Entity bakeEntity;
		std::string bakeMessage;
	};

	EditorState& State()
	{
		static EditorState state;
		return state;
	}

	std::shared_ptr<DDGIDebugChannel> Channel(EditorApp* app)
	{
		if (!app)
			return nullptr;
		auto* renderer3D = app->Runtime().GetRenderer3D();
		if (!renderer3D)
			return nullptr;
		auto renderer = renderer3D->GetVulkanRenderer();
		return renderer ? renderer->GetDDGIDebugChannel() : nullptr;
	}

	std::shared_ptr<DDGIDebugChannel>& CachedChannel()
	{
		static std::shared_ptr<DDGIDebugChannel> channel;
		return channel;
	}

	void SetSelection(std::vector<glm::ivec3> cells)
	{
		auto& s = State();
		s.selectedCells = std::make_shared<std::vector<glm::ivec3>>(std::move(cells));
		++s.selectionVersion;
	}

	// 体积的有效形状：Gizmo 拖动缩放期间 scale != 1，按 scale 实时预览探针数
	glm::uvec3 EffectiveCounts(const DDGIVolumeComponent& volume, const Transform* transform)
	{
		glm::vec3 scale = transform ? glm::abs(transform->scale) : glm::vec3(1.0f);
		return glm::uvec3(glm::clamp(glm::round(glm::vec3(volume.counts) * scale), glm::vec3(1.0f), glm::vec3(255.0f)));
	}

	struct VolumeShape
	{
		glm::ivec3 originCell = glm::ivec3(0);
		glm::vec3 spacing = glm::vec3(1.0f);
		glm::uvec3 counts = glm::uvec3(1u);
		glm::vec3 Origin() const { return glm::vec3(originCell) * spacing; }
		glm::vec3 Extent() const { return glm::vec3(counts) * spacing; }
	};

	bool GetShape(Entity entity, VolumeShape& out)
	{
		auto* volume = entity.tryGetComponent<DDGIVolumeComponent>();
		auto* transform = entity.tryGetComponent<Transform>();
		if (!volume || !transform)
			return false;
		out.spacing = glm::max(volume->spacing, glm::vec3(0.25f));
		out.counts = EffectiveCounts(*volume, transform);
		out.originCell = DDGIVolumeOriginCell(transform->position, out.spacing, out.counts);
		return true;
	}

	glm::vec3 ProbePosition(const VolumeShape& shape, const DDGIProbeEditMap* edits, const glm::ivec3& cell)
	{
		glm::vec3 p = (glm::vec3(cell) + 0.5f) * shape.spacing;
		if (edits)
		{
			auto it = edits->find(DDGIPackCell(cell));
			if (it != edits->end() && it->second.locked)
				p += glm::clamp(it->second.offset, -shape.spacing * 0.45f, shape.spacing * 0.45f);
		}
		return p;
	}

	bool RaySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r, float& t)
	{
		const glm::vec3 oc = o - c;
		const float b = glm::dot(oc, d);
		const float cc = glm::dot(oc, oc) - r * r;
		const float h = b * b - cc;
		if (h < 0.0f)
			return false;
		t = -b - std::sqrt(h);
		return t > 0.0f;
	}

	bool RayBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx, float& t0, float& t1)
	{
		t0 = 0.0f;
		t1 = 1e30f;
		for (int a = 0; a < 3; ++a)
		{
			if (std::abs(d[a]) < 1e-8f)
			{
				if (o[a] < mn[a] || o[a] > mx[a])
					return false;
				continue;
			}
			float inv = 1.0f / d[a];
			float n = (mn[a] - o[a]) * inv;
			float f = (mx[a] - o[a]) * inv;
			if (n > f) std::swap(n, f);
			t0 = std::max(t0, n);
			t1 = std::min(t1, f);
			if (t0 > t1)
				return false;
		}
		return true;
	}

	// 把编辑后的修改表写回组件（写时复制）
	void CommitEdits(Entity entity, std::shared_ptr<DDGIProbeEditMap> edits)
	{
		auto* world = entity.getWorld();
		if (!world)
			return;
		std::shared_ptr<const DDGIProbeEditMap> value;
		if (edits && !edits->empty())
			value = std::move(edits);
		world->SubmitCommand([entity, value]() mutable -> void {
			if (auto* volume = entity.tryGetComponent<DDGIVolumeComponent>())
				volume->edits = value;
			});
	}

	std::shared_ptr<DDGIProbeEditMap> CopyEdits(const DDGIVolumeComponent& volume)
	{
		return volume.edits ? std::make_shared<DDGIProbeEditMap>(*volume.edits) : std::make_shared<DDGIProbeEditMap>();
	}

	// 去掉既不禁用也不锁定的空记录
	void PruneEdits(DDGIProbeEditMap& edits)
	{
		for (auto it = edits.begin(); it != edits.end();)
		{
			if (!it->second.disabled && !it->second.locked)
				it = edits.erase(it);
			else
				++it;
		}
	}

	std::string VolumeLabel(uint32_t id)
	{
		if (id >= kCascadeIdBase)
			return std::format("级联 {}", id - kCascadeIdBase);
		return std::format("体积 #{}", id - 1);
	}

	// ── 投影与线段裁剪 ──
	struct Projector
	{
		glm::mat4 viewProj;
		glm::vec2 pos;
		glm::vec2 size;

		ImVec2 ToScreen(const glm::vec4& clip) const
		{
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return ImVec2(pos.x + (ndc.x * 0.5f + 0.5f) * size.x, pos.y + (0.5f - ndc.y * 0.5f) * size.y);
		}

		bool Point(const glm::vec3& p, ImVec2& out) const
		{
			const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
			if (c.w <= 1e-3f)
				return false;
			out = ToScreen(c);
			return true;
		}

		void Line(ImDrawList* dl, const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) const
		{
			glm::vec4 ca = viewProj * glm::vec4(a, 1.0f);
			glm::vec4 cb = viewProj * glm::vec4(b, 1.0f);
			const float eps = 1e-3f;
			if (ca.w <= eps && cb.w <= eps)
				return;
			if (ca.w <= eps)
				ca = glm::mix(ca, cb, (eps - ca.w) / (cb.w - ca.w));
			else if (cb.w <= eps)
				cb = glm::mix(cb, ca, (eps - cb.w) / (ca.w - cb.w));
			dl->AddLine(ToScreen(ca), ToScreen(cb), color, thickness);
		}

		void Box(ImDrawList* dl, const glm::vec3& mn, const glm::vec3& mx, ImU32 color, float thickness) const
		{
			const glm::vec3 c[8] = {
				{ mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
				{ mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
			static const int e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
			for (const auto& edge : e)
				Line(dl, c[edge[0]], c[edge[1]], color, thickness);
		}
	};

	ImU32 WeightColor(float w)
	{
		w = std::clamp(w, 0.0f, 1.0f);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f - w, 0.25f + 0.75f * w, 0.2f, 1.0f));
	}
}

namespace DDGIEditor
{
	bool IsVolume(Entity entity)
	{
		auto* world = entity.getWorld();
		return entity && world && world->IsValidWorldEntity(entity) && entity.hasComponent<DDGIVolumeComponent>();
	}

	void UpdateFrame(EditorApp* app, Entity selected)
	{
		auto& s = State();
		auto channel = Channel(app);
		CachedChannel() = channel;
		if (!channel)
			return;

		const uint32_t volumeId = IsVolume(selected) ? DDGIVolumeIdFromEntity(selected.getId()) : 0u;
		if (volumeId != s.selectedVolumeId)
		{
			s.selectedVolumeId = volumeId;
			SetSelection({});
		}

		if (s.leakDebug && !s.leakFreeze && s.hovered)
		{
			s.leakUV = s.hoverUV;
			s.leakUVValid = true;
		}

		DDGIDebugSettings settings;
		settings.drawMode = s.drawMode;
		settings.probeRadius = s.probeRadius;
		settings.onlySelectedVolume = s.onlySelectedVolume && volumeId != 0;
		settings.selectedVolumeId = volumeId;
		settings.selectedCells = s.selectedCells;
		settings.selectionVersion = s.selectionVersion;
		settings.leakDebug = s.leakDebug;
		settings.leakPixelValid = s.leakUVValid;
		settings.leakPixelUV = s.leakUV;
		channel->SetSettings(settings);

		// 烘焙结果：把自动重定位算出的偏移写成锁定偏移
		DDGIBakeResult bake;
		if (channel->TakeBakeResult(bake) && bake.requestId == s.pendingBake && s.pendingBake != 0)
		{
			s.pendingBake = 0;
			Entity target = s.bakeEntity;
			if (!IsVolume(target) || bake.probes.empty())
			{
				s.bakeMessage = "烘焙失败：体积未启用或不在探针池里";
				return;
			}

			auto* volume = target.tryGetComponent<DDGIVolumeComponent>();
			auto edits = CopyEdits(*volume);
			uint32_t applied = 0;
			uint32_t invalid = 0;
			for (uint32_t i = 0; i < uint32_t(bake.probes.size()); ++i)
			{
				const glm::vec4& p = bake.probes[i];
				if (!(p.w > 0.0f))
				{
					++invalid;
					continue;
				}
				if (glm::dot(glm::vec3(p), glm::vec3(p)) < 1e-8f)
					continue;
				const glm::ivec3 local(i % bake.counts.x, (i / bake.counts.x) % bake.counts.y, i / (bake.counts.x * bake.counts.y));
				auto& edit = (*edits)[DDGIPackCell(bake.originCell + local)];
				if (edit.locked)
					continue;		// 已手动锁定的不覆盖
				edit.locked = true;
				edit.offset = glm::vec3(p);
				++applied;
			}
			PruneEdits(*edits);
			CommitEdits(target, edits);
			s.bakeMessage = std::format("已写入 {} 个锁定偏移（{} 个探针尚未更新过）", applied, invalid);
		}
	}

	void SetViewportHover(bool hovered, const glm::vec2& uv)
	{
		auto& s = State();
		s.hovered = hovered;
		if (hovered)
			s.hoverUV = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
	}

	bool HandleViewportClick(Entity selected, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, bool additive)
	{
		auto& s = State();
		if (!s.probeEditMode || !IsVolume(selected))
			return false;

		VolumeShape shape;
		if (!GetShape(selected, shape))
			return false;

		const auto* volume = selected.tryGetComponent<DDGIVolumeComponent>();
		const DDGIProbeEditMap* edits = volume->edits.get();
		const glm::vec3 d = glm::normalize(rayDirection);
		const float minSpacing = std::min(shape.spacing.x, std::min(shape.spacing.y, shape.spacing.z));
		const float radius = std::max(s.probeRadius, 0.15f) * minSpacing * 1.5f;

		bool hit = false;
		glm::ivec3 hitCell(0);
		float t0, t1;
		const glm::vec3 mn = shape.Origin();
		const glm::vec3 mx = mn + shape.Extent();
		if (RayBox(rayOrigin, d, mn - glm::vec3(radius), mx + glm::vec3(radius), t0, t1))
		{
			// 沿射线步进，检查附近 3×3×3 个格子的探针球，取第一个命中
			const float step = minSpacing * 0.25f;
			const glm::ivec3 maxLocal = glm::ivec3(shape.counts) - 1;
			for (float t = t0; t <= t1 + step && !hit; t += step)
			{
				const glm::vec3 p = rayOrigin + d * t;
				const glm::ivec3 local = glm::ivec3(glm::floor((p - mn) / shape.spacing));
				float best = 1e30f;
				for (int z = -1; z <= 1; ++z)
				for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
				{
					const glm::ivec3 l = local + glm::ivec3(x, y, z);
					if (glm::any(glm::lessThan(l, glm::ivec3(0))) || glm::any(glm::greaterThan(l, maxLocal)))
						continue;
					const glm::ivec3 cell = shape.originCell + l;
					float th;
					if (RaySphere(rayOrigin, d, ProbePosition(shape, edits, cell), radius, th) && th < best)
					{
						best = th;
						hitCell = cell;
						hit = true;
					}
				}
			}
		}

		std::vector<glm::ivec3> cells = additive ? *s.selectedCells : std::vector<glm::ivec3>{};
		if (hit)
		{
			auto it = std::find(cells.begin(), cells.end(), hitCell);
			if (it != cells.end() && additive)
				cells.erase(it);
			else if (it == cells.end())
				cells.push_back(hitCell);
			s.pickLocal = hitCell - shape.originCell;
		}
		SetSelection(std::move(cells));
		return true;
	}

	void DrawViewportOverlay(EditorApp* app, Entity selected, const glm::mat4& view, const glm::mat4& projectionGL,
		const glm::vec2& viewportPos, const glm::vec2& viewportSize)
	{
		auto& s = State();
		auto channel = CachedChannel();
		if (!app || !channel)
			return;

		const auto option = app->GetOption();
		if (!option.flags.ddgiOn)
			return;

		Projector proj{ projectionGL * view, viewportPos, viewportSize };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->PushClipRect(ImVec2(viewportPos.x, viewportPos.y), ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);

		const bool selectedIsVolume = IsVolume(selected);
		const uint32_t selectedId = selectedIsVolume ? DDGIVolumeIdFromEntity(selected.getId()) : 0u;

		// 体积线框：开了探针可视化时显示全部，否则只显示选中的
		if (s.drawVolumeBoxes)
		{
			const auto stats = channel->GetStats();
			for (const auto& v : stats.volumes)
			{
				const bool isSelected = v.id == selectedId;
				if (!option.flags.ddgiProbeDrawOn && !isSelected)
					continue;
				ImU32 color = v.dropped ? IM_COL32(255, 60, 60, 220)
					: isSelected ? IM_COL32(255, 255, 255, 255)
					: v.cascade ? IM_COL32(80, 200, 255, 110)
					: IM_COL32(255, 170, 60, 200);
				proj.Box(dl, v.origin, v.origin + glm::vec3(v.counts) * v.spacing, color, isSelected ? 2.0f : 1.0f);
			}
		}

		// 选中体积：组件当前形状（拖动中或未启用时 stats 里还没有）+ 选中的探针
		VolumeShape shape;
		if (selectedIsVolume && GetShape(selected, shape))
		{
			proj.Box(dl, shape.Origin(), shape.Origin() + shape.Extent(), IM_COL32(255, 255, 255, 120), 1.0f);

			const auto* volume = selected.tryGetComponent<DDGIVolumeComponent>();
			const DDGIProbeEditMap* edits = volume->edits.get();
			for (const auto& cell : *s.selectedCells)
			{
				ImVec2 p;
				if (proj.Point(ProbePosition(shape, edits, cell), p))
					dl->AddCircle(p, 7.0f, IM_COL32(255, 255, 255, 255), 16, 2.0f);
			}

			// 已禁用 / 锁定的探针标个小记号（探针可视化关闭时也能看到修改在哪）
			if (edits && s.probeEditMode)
			{
				for (const auto& [key, edit] : *edits)
				{
					ImVec2 p;
					if (!proj.Point(ProbePosition(shape, edits, DDGIUnpackCell(key)), p))
						continue;
					if (edit.disabled)
						dl->AddLine(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 60, 60, 255), 2.0f);
					if (edit.locked)
						dl->AddRect(ImVec2(p.x - 3, p.y - 3), ImVec2(p.x + 3, p.y + 3), IM_COL32(0, 230, 230, 255));
				}
			}
		}

		// 漏光调试：采样点 -> 各探针连线，颜色 / 粗细 = 归一化后的最终权重
		if (s.leakDebug)
		{
			const auto leak = channel->GetLeak();
			if (leak.valid)
			{
				float sum[DDGIAtlasLayout_LeakVolumes] = {};
				for (const auto& p : leak.probes)
					if (p.volumeIndex < DDGIAtlasLayout_LeakVolumes)
						sum[p.volumeIndex] += p.wFinal;

				for (const auto& p : leak.probes)
				{
					const float total = p.volumeIndex < DDGIAtlasLayout_LeakVolumes ? sum[p.volumeIndex] : 0.0f;
					const float wn = total > 1e-6f ? p.wFinal / total : 0.0f;
					const float volumeWeight = p.volumeIndex < leak.volumeCount ? leak.volumeWeights[p.volumeIndex] : 0.0f;
					const ImU32 color = !p.valid ? IM_COL32(140, 140, 140, 160) : WeightColor(wn);
					proj.Line(dl, leak.biasedPos, p.position, color, 1.0f + 4.0f * wn * std::max(volumeWeight, 0.25f));
					ImVec2 sp;
					if (proj.Point(p.position, sp))
					{
						dl->AddCircleFilled(sp, 3.5f, color);
						if (p.valid && wn > 0.01f)
							dl->AddText(ImVec2(sp.x + 5, sp.y - 6), IM_COL32(255, 255, 255, 230), std::format("{:.2f}", wn).c_str());
					}
				}

				ImVec2 a, b;
				if (proj.Point(leak.worldPos, a))
				{
					dl->AddCircleFilled(a, 4.0f, IM_COL32(255, 255, 0, 255));
					if (proj.Point(leak.worldPos + leak.normal * 0.3f, b))
						dl->AddLine(a, b, IM_COL32(255, 255, 0, 255), 2.0f);
				}
			}
			if (s.leakUVValid)
			{
				const ImVec2 c(viewportPos.x + s.leakUV.x * viewportSize.x, viewportPos.y + s.leakUV.y * viewportSize.y);
				const ImU32 color = s.leakFreeze ? IM_COL32(255, 80, 80, 255) : IM_COL32(255, 255, 0, 200);
				dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), color, 1.5f);
				dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), color, 1.5f);
			}
		}

		dl->PopClipRect();
	}

	void DrawDebugOptions(EditorApp* app)
	{
		auto& s = State();
		if (!ImGui::CollapsingHeader("DDGI 调试"))
			return;
		ImGui::PushID("DDGIDebug");

		const auto option = app ? app->GetOption() : Scene3DRenderOption{};
		ImGui::TextDisabled("探针球的总开关：PostProcessFlags / ddgiProbeDrawOn");

		const char* modes[] = { "辐照度", "状态", "命中距离" };
		int mode = int(s.drawMode);
		PropertiesHelper::SetNextPropertyFieldWidth(160.0f, 0.55f);
		if (ImGui::Combo("显示模式", &mode, modes, IM_ARRAYSIZE(modes)))
			s.drawMode = DDGIProbeDrawMode(mode);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("状态：绿=正常 蓝=未更新 红=埋在几何里 黄=已自动偏移\n青=锁定 深红=禁用 灰=被高优先级体积覆盖（只在此模式显示）\n命中距离：近=红 远=蓝");
		PropertiesHelper::SetNextPropertyFieldWidth(160.0f, 0.55f);
		ImGui::SliderFloat("球半径（×间距）", &s.probeRadius, 0.02f, 0.5f, "%.2f");
		ImGui::Checkbox("只显示选中体积", &s.onlySelectedVolume);
		ImGui::SameLine();
		ImGui::Checkbox("体积线框", &s.drawVolumeBoxes);

		ImGui::Separator();
		ImGui::Checkbox("漏光调试（鼠标悬停像素）", &s.leakDebug);
		if (s.leakDebug)
		{
			ImGui::SameLine();
			ImGui::Checkbox("冻结", &s.leakFreeze);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("冻结后像素不再跟随鼠标，可以移动相机观察连线。视口里按 F 也可切换");
		}

		ImGui::Separator();
		auto channel = CachedChannel();
		if (!channel)
		{
			ImGui::TextDisabled("渲染器未就绪");
			ImGui::PopID();
			return;
		}

		const auto stats = channel->GetStats();
		if (!option.flags.ddgiOn)
			ImGui::TextDisabled("DDGI 未开启");
		else if (stats.volumes.empty())
			ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "没有可用的探针体积：打开级联或放置 DDGI 体积");

		ImGui::Text("探针 %u / %u，本帧更新 %u（预算 %u）", stats.usedProbes, stats.capacity, stats.updatedProbes, stats.budget);
		ImGui::Text("显存约 %.0f MB（按容量常驻）", double(stats.capacity) * kBytesPerProbe / (1024.0 * 1024.0));

		if (!stats.volumes.empty() && ImGui::BeginTable("DDGIVolumes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("体积");
			ImGui::TableSetupColumn("探针数");
			ImGui::TableSetupColumn("间距");
			ImGui::TableSetupColumn("槽位");
			ImGui::TableSetupColumn("更新周期");
			ImGui::TableHeadersRow();
			for (const auto& v : stats.volumes)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (v.dropped)
					ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s（超容量停用）", VolumeLabel(v.id).c_str());
				else
					ImGui::TextUnformatted(VolumeLabel(v.id).c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%ux%ux%u", v.counts.x, v.counts.y, v.counts.z);
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", double(std::min(v.spacing.x, std::min(v.spacing.y, v.spacing.z))));
				ImGui::TableNextColumn();
				if (v.dropped) ImGui::TextDisabled("-");
				else ImGui::Text("%u+%u", v.base, v.probeCount);
				ImGui::TableNextColumn();
				if (v.dropped) ImGui::TextDisabled("-");
				else ImGui::Text("1/%u 帧", v.updatePeriod);
			}
			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	void DrawLeakWindow(EditorApp* app)
	{
		auto& s = State();
		if (!s.leakDebug)
			return;

		// 视口里按 F 切换冻结
		if (s.hovered && ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().WantTextInput)
			s.leakFreeze = !s.leakFreeze;

		if (!ImGui::Begin("DDGI 漏光调试", &s.leakDebug))
		{
			ImGui::End();
			return;
		}

		auto channel = CachedChannel();
		const auto leak = channel ? channel->GetLeak() : DDGILeakResult{};
		ImGui::Checkbox("冻结像素", &s.leakFreeze);
		ImGui::SameLine();
		ImGui::TextDisabled("(视口按 F)");

		if (!leak.valid)
		{
			ImGui::TextDisabled("把鼠标移到视口里的几何上（天空像素没有数据）");
			ImGui::End();
			return;
		}

		ImGui::Text("位置 (%.2f, %.2f, %.2f)  法线 (%.2f, %.2f, %.2f)",
			leak.worldPos.x, leak.worldPos.y, leak.worldPos.z, leak.normal.x, leak.normal.y, leak.normal.z);
		ImGui::Text("偏移后采样点 (%.2f, %.2f, %.2f)", leak.biasedPos.x, leak.biasedPos.y, leak.biasedPos.z);
		ImGui::Text("结果 Lbar = (%.3f, %.3f, %.3f)", leak.irradiance.x, leak.irradiance.y, leak.irradiance.z);

		ImGui::Separator();
		float remaining = 1.0f;
		for (uint32_t i = 0; i < leak.volumeCount; ++i)
		{
			ImGui::Text("%s：权重 %.3f", VolumeLabel(leak.volumeIds[i]).c_str(), leak.volumeWeights[i]);
			remaining -= leak.volumeWeights[i];
		}
		if (leak.volumeCount == 0)
			ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "该点不在任何体积内");
		else if (remaining > 0.01f)
			ImGui::TextDisabled("未覆盖：%.3f（这部分没有间接光）", std::max(0.0f, remaining));

		ImGui::Separator();
		ImGui::TextDisabled("可见性低 = 探针被挡住；距离明显大于平均命中距离却可见性高，多半就是漏光来源");

		float sum[DDGIAtlasLayout_LeakVolumes] = {};
		for (const auto& p : leak.probes)
			if (p.volumeIndex < DDGIAtlasLayout_LeakVolumes)
				sum[p.volumeIndex] += p.wFinal;

		if (ImGui::BeginTable("DDGILeakProbes", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, 300.0f)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("体积");
			ImGui::TableSetupColumn("格子");
			ImGui::TableSetupColumn("三线性");
			ImGui::TableSetupColumn("背面");
			ImGui::TableSetupColumn("可见性");
			ImGui::TableSetupColumn("剔除");
			ImGui::TableSetupColumn("最终(归一)");
			ImGui::TableSetupColumn("距离");
			ImGui::TableSetupColumn("平均命中");
			ImGui::TableHeadersRow();

			for (const auto& p : leak.probes)
			{
				const float total = p.volumeIndex < DDGIAtlasLayout_LeakVolumes ? sum[p.volumeIndex] : 0.0f;
				const float wn = total > 1e-6f ? p.wFinal / total : 0.0f;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.volumeIndex < leak.volumeCount ? VolumeLabel(leak.volumeIds[p.volumeIndex]).c_str() : "?");
				ImGui::TableNextColumn();
				ImGui::Text("%d,%d,%d", p.cell.x, p.cell.y, p.cell.z);
				if (!p.valid)
				{
					ImGui::TableNextColumn();
					ImGui::Text("%.2f", p.wTrilinear);
					ImGui::TableNextColumn();
					ImGui::TextDisabled("未更新");
					for (int k = 0; k < 5; ++k)
						ImGui::TableNextColumn();
					continue;
				}
				ImGui::TableNextColumn(); ImGui::Text("%.2f", p.wTrilinear);
				ImGui::TableNextColumn(); ImGui::Text("%.2f", p.wBackface);
				ImGui::TableNextColumn();
				if (p.wVisibility < 0.2f) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.2f", p.wVisibility);
				else ImGui::Text("%.2f", p.wVisibility);
				ImGui::TableNextColumn();
				if (p.interior < 0.5f) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.2f", 1.0f - p.interior);
				else ImGui::Text("%.2f", 1.0f - p.interior);
				ImGui::TableNextColumn();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(WeightColor(wn)), "%.3f", wn);
				ImGui::TableNextColumn(); ImGui::Text("%.2f", p.distance);
				ImGui::TableNextColumn();
				if (p.distance > p.meanDistance * 1.2f && p.wVisibility > 0.5f)
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%.2f", p.meanDistance);
				else
					ImGui::Text("%.2f", p.meanDistance);
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void DrawVolumeComponent(Entity entity, IComponent* component, ProjectManager*)
	{
		auto& s = State();
		auto* current = static_cast<DDGIVolumeComponent*>(component);
		if (!entity || !current)
			return;

		DDGIVolumeComponent v = *current;
		bool changed = false;

		changed |= ImGui::Checkbox("启用", &v.visible);

		PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.6f);
		if (ImGui::DragFloat3("间距 spacing", glm::value_ptr(v.spacing), 0.05f, 0.25f, 16.0f))
		{
			v.spacing = glm::max(v.spacing, glm::vec3(0.25f));
			changed = true;
		}
		if (v.edits && !v.edits->empty() && ImGui::IsItemHovered())
			ImGui::SetTooltip("单探针修改按世界格子保存，改间距后它们会对不上");

		int counts[3] = { int(v.counts.x), int(v.counts.y), int(v.counts.z) };
		PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.6f);
		if (ImGui::DragInt3("探针数 counts", counts, 0.2f, 1, 255))
		{
			v.counts = glm::uvec3(glm::clamp(glm::ivec3(counts[0], counts[1], counts[2]), glm::ivec3(1), glm::ivec3(255)));
			changed = true;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("也可以用 Gizmo 缩放（R），松开时按间距取整");

		PropertiesHelper::SetNextPropertyFieldWidth(130.0f, 0.55f);
		changed |= ImGui::DragInt("优先级", &v.priority, 0.2f, 0, 1000);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("越大越优先采样；所有手动体积都高于级联。\n被更高优先级体积完全覆盖的探针不更新");
		PropertiesHelper::SetNextPropertyFieldWidth(130.0f, 0.55f);
		if (ImGui::DragFloat("过渡宽度（格）", &v.fadeCells, 0.05f, 0.01f, 8.0f))
		{
			v.fadeCells = std::max(0.01f, v.fadeCells);
			changed = true;
		}
		changed |= ImGui::Checkbox("自动重定位", &v.autoRelocate);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("探针埋进几何或贴得太近时自动挪开（最多 ±0.45 格）。锁定的探针不受影响");

		// 只读信息
		const auto* transform = entity.tryGetComponent<Transform>();
		const glm::uvec3 effective = EffectiveCounts(v, transform);
		const uint64_t total = uint64_t(effective.x) * effective.y * effective.z;
		const glm::vec3 extent = glm::vec3(effective) * v.spacing;
		ImGui::TextDisabled("%.1f x %.1f x %.1f m / %llu 探针 / 约 %.1f MB",
			extent.x, extent.y, extent.z, (unsigned long long)total, double(total) * kBytesPerProbe / (1024.0 * 1024.0));

		auto channel = CachedChannel();
		if (channel)
		{
			const auto stats = channel->GetStats();
			const uint32_t id = DDGIVolumeIdFromEntity(entity.getId());
			auto it = std::find_if(stats.volumes.begin(), stats.volumes.end(), [id](const auto& info) { return info.id == id; });
			if (it == stats.volumes.end())
				ImGui::TextDisabled(v.visible ? "尚未进入探针池（DDGI 未开启？）" : "已停用");
			else if (it->dropped)
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "超出探针总容量（%u），已停用：调低优先级更低的体积或减少探针", stats.capacity);
			else
				ImGui::TextDisabled("槽位 %u+%u，每 %u 帧更新一次", it->base, it->probeCount, it->updatePeriod);
		}

		if (changed)
		{
			if (auto* world = entity.getWorld())
			{
				world->SubmitCommand([entity, v]() mutable -> void {
					if (auto* target = entity.tryGetComponent<DDGIVolumeComponent>())
					{
						v.edits = target->edits;	// 修改表由下面的探针编辑单独提交，这里不覆盖
						*target = std::move(v);
					}
					});
			}
		}

		// ── 单探针编辑 ──
		ImGui::Separator();
		ImGui::Checkbox("探针编辑模式", &s.probeEditMode);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("在视口里点选探针（Shift 多选 / 反选）。\n建议同时打开探针可视化，显示模式选「状态」");

		VolumeShape shape;
		if (!GetShape(entity, shape))
			return;

		const size_t editCount = v.edits ? v.edits->size() : 0;
		ImGui::TextDisabled("已修改 %zu 个探针，选中 %zu 个", editCount, s.selectedCells->size());

		int pick[3] = { s.pickLocal.x, s.pickLocal.y, s.pickLocal.z };
		PropertiesHelper::SetNextPropertyFieldWidth(160.0f, 0.45f);
		if (ImGui::DragInt3("##pickLocal", pick, 0.1f, 0, 254))
			s.pickLocal = glm::clamp(glm::ivec3(pick[0], pick[1], pick[2]), glm::ivec3(0), glm::ivec3(shape.counts) - 1);
		ImGui::SameLine();
		if (ImGui::SmallButton("按坐标选择"))
			SetSelection({ shape.originCell + s.pickLocal });
		ImGui::SameLine();
		if (ImGui::SmallButton("清空选择"))
			SetSelection({});

		if (!s.selectedCells->empty())
		{
			const glm::ivec3 first = (*s.selectedCells)[0];
			DDGIProbeEdit edit;
			if (v.edits)
			{
				auto it = v.edits->find(DDGIPackCell(first));
				if (it != v.edits->end())
					edit = it->second;
			}
			const glm::ivec3 local = first - shape.originCell;
			ImGui::Text("探针 (%d, %d, %d)%s", local.x, local.y, local.z, s.selectedCells->size() > 1 ? " 等" : "");

			bool disabled = edit.disabled;
			bool locked = edit.locked;
			glm::vec3 offset = edit.offset;
			bool editChanged = false;
			bool offsetChanged = false;

			editChanged |= ImGui::Checkbox("禁用", &disabled);
			ImGui::SameLine();
			editChanged |= ImGui::Checkbox("锁定（使用手动偏移）", &locked);
			if (!locked)
				ImGui::BeginDisabled();
			const glm::vec3 limit = shape.spacing * 0.45f;
			PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.6f);
			if (ImGui::DragFloat3("偏移（米）", glm::value_ptr(offset), 0.01f))
			{
				offset = glm::clamp(offset, -limit, limit);
				offsetChanged = true;
			}
			if (!locked)
				ImGui::EndDisabled();

			if (editChanged || offsetChanged)
			{
				auto edits = CopyEdits(v);
				for (const auto& cell : *s.selectedCells)
				{
					auto& e = (*edits)[DDGIPackCell(cell)];
					e.disabled = disabled;
					e.locked = locked;
					if (offsetChanged)
						e.offset = offset;
				}
				PruneEdits(*edits);
				CommitEdits(entity, edits);
			}

			if (ImGui::SmallButton("清除选中探针的修改"))
			{
				auto edits = CopyEdits(v);
				for (const auto& cell : *s.selectedCells)
					edits->erase(DDGIPackCell(cell));
				CommitEdits(entity, edits);
			}
		}

		if (editCount > 0)
		{
			if (ImGui::SmallButton("清除全部修改"))
				CommitEdits(entity, nullptr);
			ImGui::SameLine();
			if (ImGui::SmallButton("选中全部已修改"))
			{
				std::vector<glm::ivec3> cells;
				for (const auto& [key, edit] : *v.edits)
					cells.push_back(DDGIUnpackCell(key));
				SetSelection(std::move(cells));
			}
		}

		if (ImGui::Button("烘焙自动重定位 → 锁定偏移"))
		{
			if (channel && v.visible)
			{
				s.pendingBake = channel->RequestBake(DDGIVolumeIdFromEntity(entity.getId()));
				s.bakeEntity = entity;
				s.bakeMessage = "烘焙中...";
			}
			else
				s.bakeMessage = "体积未启用";
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("把 GPU 自动重定位算出的偏移读回，写成锁定偏移存进场景。\n需要开启自动重定位并让探针先更新几帧");
		if (!s.bakeMessage.empty())
			ImGui::TextDisabled("%s", s.bakeMessage.c_str());
	}

	void FinalizeVolumeTransform(Entity entity)
	{
		auto* world = entity.getWorld();
		if (!world)
			return;
		world->SubmitCommand([entity]() -> void {
			auto* volume = entity.tryGetComponent<DDGIVolumeComponent>();
			auto* transform = entity.tryGetComponent<Transform>();
			if (!volume || !transform)
				return;
			volume->counts = EffectiveCounts(*volume, transform);
			transform->scale = glm::vec3(1.0f);
			transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

			// 中心吸附：最小角落在 spacing 网格上
			const glm::vec3 spacing = glm::max(volume->spacing, glm::vec3(0.25f));
			const glm::ivec3 origin = DDGIVolumeOriginCell(transform->position, spacing, volume->counts);
			transform->position = glm::vec3(origin) * spacing + glm::vec3(volume->counts) * spacing * 0.5f;

			if (auto* physics = entity.tryGetComponent<Physics>())
				physics->forceSyncTransform = true;
			});
	}
}
