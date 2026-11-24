#include "LogicProfilerPanel.h"
#include "EditorApp.h"
#include "ECSCore/World.h"
#include "ECSCore/LogicProfiler.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

namespace
{
	using FramePtr = std::shared_ptr<const LogicFrameProfile>;
	using LayoutPtr = std::shared_ptr<const LogicSystemLayout>;

	constexpr size_t kPhaseCount = size_t(LogicSystemPhase::Count);
	constexpr size_t kSectionCount = size_t(LogicRenderSection::Count);

	const ImVec4 kRed(0.86f, 0.27f, 0.23f, 1.0f);
	const ImVec4 kYellow(0.85f, 0.70f, 0.20f, 1.0f);
	const ImVec4 kGreen(0.36f, 0.75f, 0.45f, 1.0f);

	double Ms(double us) { return us / 1000.0; }

	// 一组 tick 上的统计量（微秒）
	struct Stat
	{
		double sum = 0.0;
		int64_t max = 0;
		int64_t cur = 0;
		int count = 0;

		void Push(int64_t v) { sum += double(v); max = std::max(max, v); ++count; }
		double Avg() const { return count > 0 ? sum / count : 0.0; }
	};

	enum class ValueMode : int { Current = 0, Average, Max };

	double Value(const Stat& st, ValueMode mode)
	{
		switch (mode)
		{
		case ValueMode::Current: return double(st.cur);
		case ValueMode::Max:     return double(st.max);
		default:                 return st.Avg();
		}
	}

	// 一个逻辑 tick：上一次逻辑更新之后到这一次逻辑更新（含）的所有 World::update 调用
	struct Tick
	{
		const LogicFrameProfile* logic = nullptr;	// 跑了 pre/update/post 的那次调用
		int calls = 0;
		int64_t workUs = 0;							// 各次调用耗时之和（不含调用之间场景循环的睡眠）
		int64_t commandsUs = 0;
		int fixedSteps = 0;
		bool catchUp = false;
		std::vector<std::array<int64_t, kPhaseCount>> systems;	// 下标同 logic->layout

		// 逻辑节拍对应的真实时间预算（时间膨胀下节拍按膨胀后的时间算）
		double BudgetUs() const
		{
			if (!logic || logic->timeScale <= 1e-4f)
				return 0.0;
			return double(logic->logicBudgetMs) / double(logic->timeScale) * 1000.0;
		}

		int64_t SystemTotal(size_t i) const
		{
			int64_t t = 0;
			for (int64_t v : systems[i])
				t += v;
			return t;
		}
	};

	std::vector<Tick> BuildTicks(const std::vector<FramePtr>& frames)
	{
		std::vector<Tick> ticks;
		Tick pending;
		LayoutPtr pendingLayout;
		bool first = true;
		for (const auto& fp : frames)
		{
			const LogicFrameProfile* f = fp.get();
			if (!f)
				continue;
			// System 布局变了（注册 / 排序）：之前攒的各 System 数据对不上，丢掉
			if (f->layout != pendingLayout || pending.systems.size() != f->systems.size())
			{
				pendingLayout = f->layout;
				pending.systems.assign(f->systems.size(), {});
			}

			++pending.calls;
			pending.workUs += f->TotalUs();
			pending.commandsUs += f->commandsUs;
			pending.fixedSteps += f->fixedSteps;
			pending.catchUp |= f->fixedCatchUp;
			for (size_t i = 0; i < f->systems.size(); ++i)
				for (size_t p = 0; p < kPhaseCount; ++p)
					pending.systems[i][p] += f->systems[i].us[p];

			if (f->logicUpdated)
			{
				pending.logic = f;
				// 历史开头那个 tick 前面的调用可能已经被挤出去了，不完整，不要
				if (!first)
					ticks.push_back(std::move(pending));
				first = false;
				pending = Tick{};
				pendingLayout.reset();
			}
		}
		return ticks;
	}

	struct SystemStat
	{
		std::array<Stat, kPhaseCount> phase;
		Stat total;
		bool enabled = true;
	};

	struct CounterStat
	{
		std::string name;
		int64_t cur = 0;
		int64_t max = 0;
		bool present = false;
	};

	struct Aggregate
	{
		std::vector<const Tick*> ticks;				// 统计范围内的 tick（旧 → 新）
		const Tick* current = nullptr;
		LayoutPtr layout;

		Stat work;
		Stat commands;
		Stat systemsSum;
		Stat other;									// work - commands - 各 System 之和（World 框架开销、事件等）
		std::vector<SystemStat> systems;

		double budgetUs = 0.0;
		double logicHz = 0.0;
		double callsPerSec = 0.0;
		int overBudget = 0;
		int catchUps = 0;

		// RenderSystem
		int renderTicks = 0;
		Stat collect;
		Stat submit;
		Stat sectionSum;
		std::array<Stat, kSectionCount> sections;
		bool submitted3D = false, submitted2D = false, submittedUI = false;

		// 规模
		Stat entities;
		std::vector<CounterStat> counters;
	};

	struct PanelState
	{
		bool visible = false;
		bool paused = false;
		int windowTicks = 120;
		int viewTick = -1;							// 暂停时查看的 tick（ticks 下标，-1 = 最新）
		std::vector<FramePtr> frames;
		std::vector<Tick> ticks;

		ValueMode valueMode = ValueMode::Average;
		bool sortSystems = false;
		bool hideIdle = true;						// 隐藏耗时为 0 的 System（没重写任何阶段）
		std::weak_ptr<LogicProfilerChannel> lastChannel;
	};

	PanelState& State()
	{
		static PanelState state;
		return state;
	}

	std::shared_ptr<LogicProfilerChannel> Channel(EditorApp* app)
	{
		if (!app)
			return nullptr;
		World* world = app->GetWorld();
		return world ? world->GetLogicProfiler() : nullptr;
	}

	Aggregate BuildAggregate(const PanelState& s)
	{
		Aggregate a;
		const int n = int(s.ticks.size());
		if (n == 0)
			return a;

		const int last = (s.paused && s.viewTick >= 0 && s.viewTick < n) ? s.viewTick : n - 1;
		const int first = std::max(0, last - s.windowTicks + 1);
		for (int i = first; i <= last; ++i)
			a.ticks.push_back(&s.ticks[i]);

		a.current = a.ticks.back();
		a.layout = a.current->logic->layout;
		const size_t systemCount = a.layout ? a.layout->names.size() : 0;
		a.systems.resize(systemCount);
		a.budgetUs = a.current->BudgetUs();

		for (const Tick* t : a.ticks)
		{
			const LogicFrameProfile* f = t->logic;
			a.work.Push(t->workUs);
			a.commands.Push(t->commandsUs);
			if (t->BudgetUs() > 0.0 && double(t->workUs) > t->BudgetUs())
				++a.overBudget;
			if (t->catchUp)
				++a.catchUps;

			int64_t sysSum = 0;
			const bool layoutOk = f->layout == a.layout && t->systems.size() == systemCount;
			for (size_t i = 0; layoutOk && i < systemCount; ++i)
			{
				auto& ss = a.systems[i];
				for (size_t p = 0; p < kPhaseCount; ++p)
					ss.phase[p].Push(t->systems[i][p]);
				const int64_t total = t->SystemTotal(i);
				ss.total.Push(total);
				sysSum += total;
			}
			if (layoutOk)
			{
				a.systemsSum.Push(sysSum);
				a.other.Push(std::max<int64_t>(0, t->workUs - t->commandsUs - sysSum));
			}

			if (f->renderCollected)
			{
				++a.renderTicks;
				a.collect.Push(f->renderCollectUs);
				a.submit.Push(f->renderSubmitUs);
				int64_t sum = 0;
				for (size_t k = 0; k < kSectionCount; ++k)
				{
					const int64_t v = f->RenderSection(LogicRenderSection(k));
					a.sections[k].Push(v);
					sum += v;
				}
				a.sectionSum.Push(sum);
			}

			a.entities.Push(f->entityCount);
			for (const auto& [name, value] : f->counters)
			{
				auto it = std::find_if(a.counters.begin(), a.counters.end(), [&](const CounterStat& c) { return c.name == name; });
				if (it == a.counters.end())
				{
					a.counters.push_back({ name, 0, value, false });
					it = a.counters.end() - 1;
				}
				it->max = std::max(it->max, value);
			}
		}

		// 当前 tick 的值
		const Tick& cur = *a.current;
		const LogicFrameProfile* cf = cur.logic;
		a.work.cur = cur.workUs;
		a.commands.cur = cur.commandsUs;
		if (cf->layout == a.layout && cur.systems.size() == systemCount)
		{
			int64_t sysSum = 0;
			for (size_t i = 0; i < systemCount; ++i)
			{
				auto& ss = a.systems[i];
				for (size_t p = 0; p < kPhaseCount; ++p)
					ss.phase[p].cur = cur.systems[i][p];
				ss.total.cur = cur.SystemTotal(i);
				ss.enabled = i < cf->systems.size() ? cf->systems[i].enabled : true;
				sysSum += ss.total.cur;
			}
			a.systemsSum.cur = sysSum;
			a.other.cur = std::max<int64_t>(0, cur.workUs - cur.commandsUs - sysSum);
		}
		if (cf->renderCollected)
		{
			a.collect.cur = cf->renderCollectUs;
			a.submit.cur = cf->renderSubmitUs;
			int64_t sum = 0;
			for (size_t k = 0; k < kSectionCount; ++k)
			{
				a.sections[k].cur = cf->RenderSection(LogicRenderSection(k));
				sum += a.sections[k].cur;
			}
			a.sectionSum.cur = sum;
			a.submitted3D = cf->submitted3D;
			a.submitted2D = cf->submitted2D;
			a.submittedUI = cf->submittedUI;
		}
		a.entities.cur = cf->entityCount;
		for (auto& c : a.counters)
		{
			for (const auto& [name, value] : cf->counters)
			{
				if (name == c.name)
				{
					c.cur = value;
					c.present = true;
				}
			}
		}

		// 频率按相邻逻辑更新开始的间隔算
		if (a.ticks.size() >= 2)
		{
			const double span = double(a.ticks.back()->logic->startUs - a.ticks.front()->logic->startUs);
			if (span > 0.0)
			{
				a.logicHz = double(a.ticks.size() - 1) * 1e6 / span;
				int calls = 0;
				for (size_t i = 1; i < a.ticks.size(); ++i)
					calls += a.ticks[i]->calls;
				a.callsPerSec = double(calls) * 1e6 / span;
			}
		}
		return a;
	}

	// ───────────────────────────── 顶部 ─────────────────────────────

	void DrawHeader(PanelState& s, const Aggregate& agg, LogicProfilerChannel& channel)
	{
		if (ImGui::Button(s.paused ? "继续" : "暂停"))
		{
			s.paused = !s.paused;
			s.viewTick = -1;
		}
		ImGui::SameLine();
		if (ImGui::Button("清空"))
		{
			channel.Clear();
			s.frames.clear();
			s.ticks.clear();
			s.viewTick = -1;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		ImGui::SliderInt("统计 tick 数", &s.windowTicks, 10, 300);
		if (s.paused && !s.ticks.empty())
		{
			const int n = int(s.ticks.size());
			int v = (s.viewTick < 0 || s.viewTick >= n) ? n - 1 : s.viewTick;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::SliderInt("查看 tick", &v, 0, n - 1))
				s.viewTick = v;
		}

		if (!agg.current)
		{
			ImGui::TextDisabled("等待数据……（World 需要在运行，且至少有两次逻辑更新）");
			return;
		}

		const double targetHz = agg.budgetUs > 0.0 ? 1e6 / agg.budgetUs : 0.0;
		ImGui::Text("逻辑 %.1f Hz（节拍 %.1f Hz）    World::update %.0f 次/秒    tick 耗时 %.2f ms（平均 %.2f / 最大 %.2f，预算 %.2f）",
			agg.logicHz, targetHz, agg.callsPerSec,
			Ms(double(agg.work.cur)), Ms(agg.work.Avg()), Ms(double(agg.work.max)), Ms(agg.budgetUs));
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"逻辑 tick = 上一次逻辑更新之后到这一次逻辑更新（含）之间所有的 World::update 调用；\n"
				"tick 耗时 = 这些调用本身的耗时之和，不含场景循环在调用之间的睡眠。\n"
				"预算 = 逻辑节拍（World 的 logicDeltaTime，时间膨胀下换算成真实时间）。\n"
				"tick 耗时超过预算，逻辑就跟不上节拍；逻辑帧率低于渲染面板的帧率时，\n"
				"渲染那边有一部分帧是在重复提交同一份场景数据。\n"
				"全部是墙钟时间；Debug 构建下数字会明显偏大。");

		// 结论
		const double workAvg = agg.work.Avg();
		if (agg.budgetUs > 0.0 && workAvg > agg.budgetUs)
			ImGui::TextColored(kRed, "逻辑跟不上：平均 tick 耗时超出节拍预算 %.0f%%", (workAvg / agg.budgetUs - 1.0) * 100.0);
		else if (targetHz > 0.0 && agg.logicHz < targetHz * 0.9)
			ImGui::TextColored(kYellow, "逻辑帧率低于节拍，但耗时有余量：受场景循环的调用频率 / 节拍累加限制，不是逻辑算不过来");
		else if (agg.overBudget > 0)
			ImGui::TextColored(kYellow, "按节拍运行，但统计范围内有 %d 个 tick 超出预算（偶发卡顿）", agg.overBudget);
		else
			ImGui::TextColored(kGreen, "按节拍运行，余量 %.0f%%", agg.budgetUs > 0.0 ? (1.0 - workAvg / agg.budgetUs) * 100.0 : 0.0);
		if (agg.catchUps > 0)
		{
			ImGui::SameLine();
			ImGui::TextColored(kRed, "    物理严重落后、大步长追帧 %d 次", agg.catchUps);
		}

		// 规模
		ImGui::Text("实体 %lld（最大 %lld）", (long long)agg.entities.cur, (long long)agg.entities.max);
		for (const auto& c : agg.counters)
		{
			ImGui::SameLine();
			ImGui::Text("    %s %lld（最大 %lld）", c.name.c_str(), (long long)(c.present ? c.cur : 0), (long long)c.max);
		}

		// tick 耗时曲线 + 预算线
		std::vector<float> values;
		values.reserve(agg.ticks.size());
		float maxValue = 0.0f;
		for (const Tick* t : agg.ticks)
		{
			const float v = float(Ms(double(t->workUs)));
			values.push_back(v);
			maxValue = std::max(maxValue, v);
		}
		const float budgetMs = float(Ms(agg.budgetUs));
		const float scaleMax = std::max({ 1.0f, maxValue * 1.15f, budgetMs * 1.25f });
		ImGui::PlotLines("##tickTime", values.data(), int(values.size()), 0, "逻辑 tick 耗时 (ms)，红线 = 预算",
			0.0f, scaleMax, ImVec2(-1.0f, 64.0f));
		if (budgetMs > 0.0f)
		{
			const ImVec2 pad = ImGui::GetStyle().FramePadding;
			const ImVec2 rmin = ImGui::GetItemRectMin();
			const ImVec2 rmax = ImGui::GetItemRectMax();
			const float innerTop = rmin.y + pad.y;
			const float innerBottom = rmax.y - pad.y;
			const float y = innerBottom - (budgetMs / scaleMax) * (innerBottom - innerTop);
			ImGui::GetWindowDrawList()->AddLine(ImVec2(rmin.x + pad.x, y), ImVec2(rmax.x - pad.x, y),
				ImGui::GetColorU32(kRed), 1.0f);
		}
	}

	// ───────────────────────────── System 表 ─────────────────────────────

	void DrawSystems(PanelState& s, const Aggregate& agg)
	{
		ImGui::SetNextItemWidth(90.0f);
		int mode = int(s.valueMode);
		if (ImGui::Combo("阶段列数值", &mode, "当前\0平均\0最大\0"))
			s.valueMode = ValueMode(mode);
		ImGui::SameLine();
		ImGui::Checkbox("按合计排序", &s.sortSystems);
		ImGui::SameLine();
		ImGui::Checkbox("隐藏耗时为 0 的 System", &s.hideIdle);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"每个 System 在一个逻辑 tick 内各阶段的耗时；FixedUpdate 是这个 tick 内所有固定步长之和。\n"
				"顺序 = 执行顺序（按优先级从高到低）。占比 = 平均合计 / 平均 tick 耗时。\n"
				"RenderSystem 的 PostUpdate 就是收集渲染数据并提交三缓冲，细分见「RenderSystem」页。\n"
				"「其他」= tick 耗时 - ProcessCommands - 各 System 之和：World 自己的框架开销、调用之间的零碎时间。");

		if (!agg.current || !agg.layout)
			return;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		enum Column { ColOrder = 0, ColName, ColPriority, ColPre, ColUpdate, ColPost, ColFixed, ColAvg, ColMax, ColShare, ColCount };
		if (!ImGui::BeginTable("##logicSystems", ColCount, flags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.0f);
		ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("优先级", ImGuiTableColumnFlags_WidthStretch, 0.7f);
		ImGui::TableSetupColumn("PreUpdate", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Update", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("PostUpdate", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("FixedUpdate", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("合计 平均", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("合计 最大", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableHeadersRow();

		const double workAvg = agg.work.Avg();
		auto msCell = [](int column, double us, bool dim) {
			ImGui::TableSetColumnIndex(column);
			if (us <= 0.0)
				ImGui::TextDisabled("—");
			else if (dim)
				ImGui::TextDisabled("%.3f", Ms(us));
			else
				ImGui::Text("%.3f", Ms(us));
			};
		auto shareCell = [&](double avg) {
			ImGui::TableSetColumnIndex(ColShare);
			if (workAvg <= 0.0 || avg <= 0.0)
				return;
			const float frac = float(std::clamp(avg / workAvg, 0.0, 1.0));
			char text[32];
			std::snprintf(text, sizeof(text), "%.1f%%", frac * 100.0f);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, frac > 0.5f ? kRed : (frac > 0.2f ? kYellow : kGreen));
			ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), text);
			ImGui::PopStyleColor();
			};
		auto plainRow = [&](const char* name, const Stat& st, const char* tip) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(ColName);
			ImGui::TextUnformatted(name);
			if (tip && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tip);
			msCell(ColAvg, st.Avg(), false);
			msCell(ColMax, double(st.max), false);
			shareCell(st.Avg());
			};

		// tick 总耗时
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(ColName);
		ImGui::TextUnformatted("逻辑 tick 总耗时");
		msCell(ColAvg, agg.work.Avg(), false);
		msCell(ColMax, double(agg.work.max), false);
		ImGui::TableSetColumnIndex(ColShare);
		ImGui::TextDisabled("100%%");

		plainRow("ProcessCommands", agg.commands, "World::SubmitCommand 提交的异步命令（编辑器操作等）");

		std::vector<size_t> order(agg.systems.size());
		std::iota(order.begin(), order.end(), size_t(0));
		if (s.sortSystems)
			std::stable_sort(order.begin(), order.end(),
				[&](size_t a, size_t b) { return agg.systems[a].total.Avg() > agg.systems[b].total.Avg(); });

		for (size_t i : order)
		{
			const SystemStat& ss = agg.systems[i];
			if (s.hideIdle && ss.total.max == 0)
				continue;

			ImGui::TableNextRow();
			ImGui::PushID(int(i));
			ImGui::TableSetColumnIndex(ColOrder);
			ImGui::TextDisabled("%d", int(i));
			ImGui::TableSetColumnIndex(ColName);
			const std::string& name = agg.layout->names[i];
			if (ss.enabled)
				ImGui::TextUnformatted(name.c_str());
			else
				ImGui::TextDisabled("%s（已禁用）", name.c_str());
			ImGui::TableSetColumnIndex(ColPriority);
			ImGui::TextDisabled("%d", i < agg.layout->priorities.size() ? agg.layout->priorities[i] : 0);

			const int phaseCols[kPhaseCount] = { ColPre, ColUpdate, ColPost, ColFixed };
			for (size_t p = 0; p < kPhaseCount; ++p)
				msCell(phaseCols[p], Value(ss.phase[p], s.valueMode), !ss.enabled);
			msCell(ColAvg, ss.total.Avg(), !ss.enabled);
			msCell(ColMax, double(ss.total.max), !ss.enabled);
			shareCell(ss.total.Avg());
			ImGui::PopID();
		}

		plainRow("其他", agg.other, "tick 耗时 - ProcessCommands - 各 System 之和");

		ImGui::EndTable();
	}

	// ───────────────────────────── RenderSystem ─────────────────────────────

	void DrawRenderSystem(const Aggregate& agg)
	{
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"RenderSystem::postUpdate：把各段提交到线程池并行收集渲染数据，全部做完后交换三缓冲。\n"
				"「收集整段」是墙钟时间（并行后的实际耗时）；各段是各自线程上的耗时，\n"
				"所以各段之和会大于整段，比值就是并行度。整段长通常是被最慢的那一段拖住。");
		ImGui::SameLine();

		if (!agg.current || agg.renderTicks == 0)
		{
			ImGui::TextDisabled("统计范围内 RenderSystem 没有收集过数据");
			return;
		}

		const double collectAvg = agg.collect.Avg();
		const double parallelism = collectAvg > 0.0 ? agg.sectionSum.Avg() / collectAvg : 0.0;
		ImGui::Text("收集整段 平均 %.3f ms    各段之和 %.3f ms    并行度 %.2f    提交：%s%s%s",
			Ms(collectAvg), Ms(agg.sectionSum.Avg()), parallelism,
			agg.submitted3D ? "3D " : "", agg.submitted2D ? "2D " : "", agg.submittedUI ? "UI" : "");

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		enum Column { ColName = 0, ColCur, ColAvg, ColMax, ColShare, ColCount };
		if (!ImGui::BeginTable("##logicRender", ColCount, flags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("段", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("当前 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("平均 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("最大 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("占整段", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableHeadersRow();

		auto row = [&](const char* name, const Stat& st, bool share) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(ColName);
			if (st.max == 0)
				ImGui::TextDisabled("%s", name);
			else
				ImGui::TextUnformatted(name);
			ImGui::TableSetColumnIndex(ColCur);
			ImGui::Text("%.3f", Ms(double(st.cur)));
			ImGui::TableSetColumnIndex(ColAvg);
			ImGui::Text("%.3f", Ms(st.Avg()));
			ImGui::TableSetColumnIndex(ColMax);
			ImGui::Text("%.3f", Ms(double(st.max)));
			ImGui::TableSetColumnIndex(ColShare);
			if (share && collectAvg > 0.0 && st.Avg() > 0.0)
			{
				// 并行段可能接近整段（就是它在拖），超过 100% 不会发生：单段不会比整段长
				const float frac = float(std::clamp(st.Avg() / collectAvg, 0.0, 1.0));
				char text[32];
				std::snprintf(text, sizeof(text), "%.1f%%", frac * 100.0f);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, frac > 0.8f ? kRed : (frac > 0.4f ? kYellow : kGreen));
				ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), text);
				ImGui::PopStyleColor();
			}
			};

		row("收集整段（墙钟）", agg.collect, false);
		row("三缓冲提交", agg.submit, false);

		std::vector<size_t> order(kSectionCount);
		std::iota(order.begin(), order.end(), size_t(0));
		std::stable_sort(order.begin(), order.end(),
			[&](size_t a, size_t b) { return agg.sections[a].Avg() > agg.sections[b].Avg(); });
		for (size_t k : order)
			row(LogicRenderSectionName(LogicRenderSection(k)), agg.sections[k], true);

		ImGui::EndTable();
	}
}

namespace LogicProfilerPanel
{
	bool& Visible()
	{
		return State().visible;
	}

	void Draw(EditorApp* app)
	{
		auto& s = State();
		auto channel = Channel(app);

		// 换了 World（重新打开场景）：旧通道关掉，历史清空
		auto last = s.lastChannel.lock();
		if (last != channel)
		{
			if (last)
				last->SetEnabled(false);
			s.frames.clear();
			s.ticks.clear();
			s.viewTick = -1;
			s.lastChannel = channel;
		}

		// 窗口关着或暂停时关闭统计：场景线程那边各打点处只剩一次判空
		if (channel)
			channel->SetEnabled(s.visible && !s.paused);

		if (!s.visible)
		{
			if (!s.frames.empty())
			{
				std::vector<FramePtr>().swap(s.frames);
				std::vector<Tick>().swap(s.ticks);
			}
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
		// 浮动窗口，不参与停靠
		if (!ImGui::Begin("逻辑性能", &s.visible, ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		if (!channel)
		{
			ImGui::TextDisabled("没有打开的场景");
			ImGui::End();
			return;
		}

		if (!s.paused)
		{
			s.frames = channel->Snapshot();
			s.ticks = BuildTicks(s.frames);
			s.viewTick = -1;
		}

		const Aggregate agg = BuildAggregate(s);
		DrawHeader(s, agg, *channel);

		if (ImGui::BeginTabBar("##logicProfilerTabs"))
		{
			if (ImGui::BeginTabItem("System"))
			{
				DrawSystems(s, agg);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("RenderSystem"))
			{
				DrawRenderSystem(agg);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
