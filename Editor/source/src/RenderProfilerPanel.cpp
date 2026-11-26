#include "RenderProfilerPanel.h"
#include "EditorApp.h"
#include "RenderEngine/Renderer.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace
{
	using ProfilePtr = std::shared_ptr<const RenderFrameProfile>;
	using LayoutPtr = std::shared_ptr<const RenderGraphProfileLayout>;

	constexpr size_t kStepCount = size_t(RenderProfileStep::Count);

	// 节点尺寸（图空间，缩放前）
	constexpr float kNodeW = 168.0f;
	constexpr float kNodeH = 44.0f;
	constexpr float kFenceW = 96.0f;
	constexpr float kFenceH = 24.0f;
	constexpr float kColStep = kNodeW + 70.0f;
	constexpr float kRowStep = kNodeH + 16.0f;

	ImVec2 V2Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
	ImVec2 V2Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
	ImVec2 V2Mul(const ImVec2& a, float s) { return ImVec2(a.x * s, a.y * s); }

	double Ms(double us) { return us / 1000.0; }

	// 一组帧上的统计量（微秒）
	struct Stat
	{
		double sum = 0.0;
		int64_t max = 0;
		int64_t cur = 0;
		int count = 0;

		void Push(int64_t v) { sum += double(v); max = std::max(max, v); ++count; }
		double Avg() const { return count > 0 ? sum / count : 0.0; }
	};

	struct PassStat
	{
		Stat execute;				// CPU：Execute 墙钟耗时，只统计执行了的帧
		Stat wait;					// 其中等待提交（等提交锁）
		Stat self;					// CPU 自身 = Execute - 等待提交
		Stat gpu;					// GPU：时间戳计时，只统计有 GPU 计时的帧
		Stat frameBegin;
		int executedFrames = 0;
		int gpuFrames = 0;
		bool executedCur = false;
		bool gpuCur = false;
	};

	enum class ValueMode : int { Current = 0, Average, Max };

	// 节点图按哪个耗时着色 / 算关键路径
	enum class GraphMetric : int { Gpu = 0, CpuSelf, CpuTotal };

	double Value(const Stat& st, ValueMode mode)
	{
		switch (mode)
		{
		case ValueMode::Current: return double(st.cur);
		case ValueMode::Max:     return double(st.max);
		default:                 return st.Avg();
		}
	}

	struct Aggregate
	{
		std::vector<const RenderFrameProfile*> frames;	// 统计范围内的帧（旧 → 新）
		const RenderFrameProfile* current = nullptr;	// 查看的那一帧（范围内最新的）
		Stat total;
		std::array<Stat, kStepCount> steps;
		LayoutPtr layout;
		std::vector<PassStat> passes;
		Stat graphGpu;				// 每帧图内各 Pass GPU 耗时之和（图内提交串行执行，和即图在 GPU 上忙的时间）
		bool hasGpu = false;		// 统计范围内有 GPU 计时
		double fps = 0.0;
	};

	// 节点图几何：只在 RenderGraph 结构变化时重算
	struct GraphGeometry
	{
		const RenderGraphProfileLayout* layout = nullptr;
		std::vector<ImVec2> pos;						// 节点左上角（图空间）
		std::vector<ImVec2> size;
		std::vector<int> layer;
		std::vector<std::pair<int, int>> edges;			// 传递约简后的边 (from, to)
		ImVec2 boundsMin = ImVec2(0, 0);
		ImVec2 boundsMax = ImVec2(0, 0);

		// 祖先位集：第 i 行是节点 i 的全部祖先（传递闭包），选中节点时高亮上游用
		int words = 0;
		std::vector<uint64_t> ancestors;
		bool IsAncestor(int x, int of) const
		{
			return ((ancestors[size_t(of) * words + x / 64] >> (x % 64)) & 1ull) != 0;
		}
	};

	struct PanelState
	{
		bool visible = false;
		bool paused = false;
		int windowFrames = 120;
		int viewFrame = -1;								// 暂停时查看的帧（frames 下标，-1 = 最新）
		std::vector<ProfilePtr> frames;

		int selectedPass = -1;
		bool scrollToPass = false;
		bool jumpToSteps = false;
		bool hideSkipped = true;
		bool sortPasses = false;

		ValueMode graphValue = ValueMode::Average;
		GraphMetric graphMetric = GraphMetric::Gpu;		// 节点图着色依据
		bool showCriticalPath = true;
		ImVec2 pan = ImVec2(0, 0);
		float zoom = 1.0f;
		bool fitRequested = true;
		LayoutPtr layoutHold;							// 保证 geometry.layout 指向的布局活着
		GraphGeometry geometry;
	};

	PanelState& State()
	{
		static PanelState state;
		return state;
	}

	std::shared_ptr<RenderProfilerChannel> Channel(EditorApp* app)
	{
		if (!app)
			return nullptr;
		auto* renderer = app->Runtime().GetRenderer();
		if (!renderer)
			return nullptr;
		return renderer->GetRenderProfiler();
	}

	// 绿 → 黄 → 红
	ImVec4 HeatColor(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const ImVec4 green(0.22f, 0.60f, 0.34f, 1.0f);
		const ImVec4 yellow(0.85f, 0.70f, 0.20f, 1.0f);
		const ImVec4 red(0.86f, 0.27f, 0.23f, 1.0f);
		const ImVec4& a = t < 0.5f ? green : yellow;
		const ImVec4& b = t < 0.5f ? yellow : red;
		const float k = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
		return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, 1.0f);
	}

	Aggregate BuildAggregate(const PanelState& s)
	{
		Aggregate a;
		const int n = int(s.frames.size());
		if (n == 0)
			return a;

		const int last = (s.paused && s.viewFrame >= 0 && s.viewFrame < n) ? s.viewFrame : n - 1;
		const int first = std::max(0, last - s.windowFrames + 1);
		for (int i = first; i <= last; ++i)
		{
			if (s.frames[i])
				a.frames.push_back(s.frames[i].get());
		}
		if (a.frames.empty())
			return a;

		a.current = a.frames.back();
		a.layout = a.current->graphLayout;
		const size_t passCount = a.layout ? a.layout->nodes.size() : 0;
		a.passes.resize(passCount);

		for (const auto* f : a.frames)
		{
			a.total.Push(f->TotalUs());
			for (size_t k = 0; k < kStepCount; ++k)
				a.steps[k].Push(f->steps[k]);

			// RenderGraph 重新编译过的帧，结构对不上，不参与 Pass 统计
			if (f->graphLayout != a.layout || f->passes.size() != passCount)
				continue;
			int64_t frameGpu = 0;
			bool frameHasGpu = false;
			for (size_t p = 0; p < passCount; ++p)
			{
				const auto& pp = f->passes[p];
				auto& ps = a.passes[p];
				ps.frameBegin.Push(pp.frameBeginUs);
				if (pp.executed)
				{
					ps.execute.Push(pp.executeUs);
					ps.wait.Push(pp.executeWaitUs);
					ps.self.Push(pp.SelfUs());
					++ps.executedFrames;
					if (pp.gpuValid)
					{
						ps.gpu.Push(pp.gpuUs);
						++ps.gpuFrames;
						frameGpu += pp.gpuUs;
						frameHasGpu = true;
					}
				}
			}
			if (frameHasGpu)
			{
				a.graphGpu.Push(frameGpu);
				a.hasGpu = true;
			}
		}

		a.total.cur = a.current->TotalUs();
		for (size_t k = 0; k < kStepCount; ++k)
			a.steps[k].cur = a.current->steps[k];
		if (a.current->passes.size() == passCount)
		{
			for (size_t p = 0; p < passCount; ++p)
			{
				const auto& pp = a.current->passes[p];
				a.passes[p].executedCur = pp.executed;
				a.passes[p].execute.cur = pp.executed ? pp.executeUs : 0;
				a.passes[p].wait.cur = pp.executed ? pp.executeWaitUs : 0;
				a.passes[p].self.cur = pp.executed ? pp.SelfUs() : 0;
				a.passes[p].gpuCur = pp.executed && pp.gpuValid;
				a.passes[p].gpu.cur = a.passes[p].gpuCur ? pp.gpuUs : 0;
				a.passes[p].frameBegin.cur = pp.frameBeginUs;
				a.graphGpu.cur += a.passes[p].gpu.cur;
			}
		}

		// 帧率按相邻帧完成的间隔算（多帧并行时单帧耗时会重叠）
		if (a.frames.size() >= 2)
		{
			const double span = double(a.frames.back()->endTimeUs - a.frames.front()->endTimeUs);
			if (span > 0.0)
				a.fps = double(a.frames.size() - 1) * 1e6 / span;
		}
		return a;
	}

	// ───────────────────────────── 节点图布局 ─────────────────────────────

	void BuildGeometry(GraphGeometry& g, const RenderGraphProfileLayout& layout)
	{
		g = GraphGeometry{};
		g.layout = &layout;
		const int n = int(layout.nodes.size());
		if (n == 0)
			return;

		auto validAfter = [n](uint32_t a, int i) { return int(a) < n && int(a) != i; };

		// 层号 = 最长前驱链长度。排序后的顺序本来就是拓扑序，迭代到不变只是防御
		g.layer.assign(n, 0);
		for (int iter = 0; iter < n; ++iter)
		{
			bool changed = false;
			for (int i = 0; i < n; ++i)
			{
				for (uint32_t a : layout.nodes[i].afters)
				{
					if (!validAfter(a, i))
						continue;
					const int l = g.layer[a] + 1;
					if (l > g.layer[i])
					{
						g.layer[i] = l;
						changed = true;
					}
				}
			}
			if (!changed)
				break;
		}

		std::vector<int> order(n);
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g.layer[a] < g.layer[b]; });

		// 传递约简：a→i 若 a 已经是 i 的另一个前驱 b 的祖先，就是冗余边，不画
		const int words = (n + 63) / 64;
		std::vector<uint64_t> ancestors(size_t(n) * words, 0);
		for (int i : order)
		{
			uint64_t* ai = &ancestors[size_t(i) * words];
			for (uint32_t a : layout.nodes[i].afters)
			{
				if (!validAfter(a, i))
					continue;
				ai[a / 64] |= 1ull << (a % 64);
				const uint64_t* aa = &ancestors[size_t(a) * words];
				for (int w = 0; w < words; ++w)
					ai[w] |= aa[w];
			}
		}
		auto isAncestor = [&](int x, int of) {
			return ((ancestors[size_t(of) * words + x / 64] >> (x % 64)) & 1ull) != 0;
			};

		std::vector<std::vector<int>> preds(n), succs(n);
		for (int i = 0; i < n; ++i)
		{
			const auto& afters = layout.nodes[i].afters;
			for (uint32_t a : afters)
			{
				if (!validAfter(a, i))
					continue;
				bool redundant = false;
				for (uint32_t b : afters)
				{
					if (b != a && validAfter(b, i) && isAncestor(int(a), int(b)))
					{
						redundant = true;
						break;
					}
				}
				if (redundant)
					continue;
				g.edges.emplace_back(int(a), i);
				preds[i].push_back(int(a));
				succs[a].push_back(i);
			}
		}

		// 同一层内的上下顺序：重心法，上下各扫两遍，减少交叉
		const int maxLayer = *std::max_element(g.layer.begin(), g.layer.end());
		std::vector<std::vector<int>> columns(maxLayer + 1);
		for (int i = 0; i < n; ++i)
			columns[g.layer[i]].push_back(i);

		std::vector<float> rank(n, 0.0f);
		auto assignRanks = [&](const std::vector<int>& column) {
			if (column.empty())
				return;
			const float half = float(column.size() - 1) * 0.5f;
			for (size_t k = 0; k < column.size(); ++k)
				rank[column[k]] = float(k) - half;
			};
		for (const auto& column : columns)
			assignRanks(column);

		for (int sweep = 0; sweep < 4; ++sweep)
		{
			const bool down = (sweep % 2) == 0;
			for (int step = 0; step < maxLayer; ++step)
			{
				const int L = down ? step + 1 : maxLayer - 1 - step;
				auto& column = columns[L];
				std::vector<std::pair<float, int>> keyed;
				keyed.reserve(column.size());
				for (int v : column)
				{
					const auto& neighbours = down ? preds[v] : succs[v];
					float key = rank[v];
					if (!neighbours.empty())
					{
						key = 0.0f;
						for (int u : neighbours)
							key += rank[u];
						key /= float(neighbours.size());
					}
					keyed.emplace_back(key, v);
				}
				std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				for (size_t k = 0; k < keyed.size(); ++k)
					column[k] = keyed[k].second;
				assignRanks(column);
			}
		}

		g.pos.resize(n);
		g.size.resize(n);
		g.boundsMin = ImVec2(FLT_MAX, FLT_MAX);
		g.boundsMax = ImVec2(-FLT_MAX, -FLT_MAX);
		for (int i = 0; i < n; ++i)
		{
			const bool fence = layout.nodes[i].fence;
			const ImVec2 size = fence ? ImVec2(kFenceW, kFenceH) : ImVec2(kNodeW, kNodeH);
			ImVec2 pos(float(g.layer[i]) * kColStep, rank[i] * kRowStep);
			pos.x += (kNodeW - size.x) * 0.5f;
			pos.y += (kNodeH - size.y) * 0.5f;
			g.pos[i] = pos;
			g.size[i] = size;
			g.boundsMin = ImVec2(std::min(g.boundsMin.x, pos.x), std::min(g.boundsMin.y, pos.y));
			g.boundsMax = ImVec2(std::max(g.boundsMax.x, pos.x + size.x), std::max(g.boundsMax.y, pos.y + size.y));
		}

		// 祖先集合留着：选中节点时直接查，图结构不变就不用重算
		g.words = words;
		g.ancestors = std::move(ancestors);
	}

	// 关键路径：按耗时最长的依赖链。同长时优先层号大的前驱，这样不会走到被约简掉的冗余边。
	// target < 0：整张图里最长的那条；target >= 0：以 target 为终点（含它自己）的最长链
	void CriticalPath(const GraphGeometry& g, const RenderGraphProfileLayout& layout, const std::vector<double>& weight,
		int target, std::vector<int>& parent, std::vector<char>& onPath, double& length, int& count)
	{
		const int n = int(layout.nodes.size());
		parent.assign(n, -1);
		onPath.assign(n, 0);
		length = 0.0;
		count = 0;
		if (n == 0)
			return;

		std::vector<int> order(n);
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g.layer[a] < g.layer[b]; });

		std::vector<double> dist(n, 0.0);
		for (int i : order)
		{
			double best = 0.0;
			int bestParent = -1;
			for (uint32_t a : layout.nodes[i].afters)
			{
				if (int(a) >= n || int(a) == i)
					continue;
				const double d = dist[a];
				if (bestParent < 0 || d > best || (d == best && g.layer[a] > g.layer[bestParent]))
				{
					best = d;
					bestParent = int(a);
				}
			}
			dist[i] = best + weight[i];
			parent[i] = bestParent;
		}

		int end = 0;
		if (target >= 0 && target < n)
			end = target;
		else
		{
			for (int i = 1; i < n; ++i)
			{
				if (dist[i] > dist[end])
					end = i;
			}
		}
		length = dist[end];
		if (length <= 0.0)
			return;
		for (int v = end; v >= 0; v = parent[v])
		{
			onPath[v] = 1;
			if (weight[v] > 0.0)
				++count;
		}
	}

	// ───────────────────────────── 顶部 ─────────────────────────────

	void DrawHeader(PanelState& s, const Aggregate& agg, RenderProfilerChannel& channel)
	{
		if (ImGui::Button(s.paused ? "继续" : "暂停"))
		{
			s.paused = !s.paused;
			s.viewFrame = -1;
		}
		ImGui::SameLine();
		if (ImGui::Button("清空"))
		{
			channel.Clear();
			s.frames.clear();
			s.viewFrame = -1;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		ImGui::SliderInt("统计帧数", &s.windowFrames, 10, 300);
		if (s.paused && !s.frames.empty())
		{
			const int n = int(s.frames.size());
			int v = (s.viewFrame < 0 || s.viewFrame >= n) ? n - 1 : s.viewFrame;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::SliderInt("查看帧", &v, 0, n - 1))
				s.viewFrame = v;
		}

		if (!agg.current)
		{
			ImGui::TextDisabled("等待数据……（场景视口需要在渲染）");
			return;
		}

		ImGui::Text("帧 #%u    单帧耗时 %.2f ms（平均 %.2f / 最大 %.2f）    帧率 %.1f FPS    被新帧顶掉 %llu 帧",
			agg.current->frameIndex,
			Ms(double(agg.total.cur)), Ms(agg.total.Avg()), Ms(double(agg.total.max)),
			agg.fps, (unsigned long long)channel.Dropped());
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"单帧耗时 = 从渲染循环取走这一帧到画完，不含「帧数据解析」和「排队」。\n"
				"多帧并行时各帧会重叠，所以帧率按相邻帧完成的间隔算。\n"
				"单帧和各步骤是墙钟时间；RenderGraph 里的 Pass 另有 GPU 计时（时间戳）。\n"
				"Debug 构建和验证层下数字会明显偏大。\n"
				"被新帧顶掉 = 候选帧还没被渲染线程取走，场景就提交了新数据，旧的那帧被替换掉；\n"
				"场景数据比渲染快时会持续增长，属正常。");

		std::vector<float> values;
		values.reserve(agg.frames.size());
		float maxValue = 0.0f;
		for (const auto* f : agg.frames)
		{
			const float v = float(Ms(double(f->TotalUs())));
			values.push_back(v);
			maxValue = std::max(maxValue, v);
		}
		ImGui::PlotLines("##frameTime", values.data(), int(values.size()), 0, "单帧耗时 (ms)",
			0.0f, std::max(1.0f, maxValue * 1.15f), ImVec2(-1.0f, 64.0f));
	}

	// ───────────────────────────── 步骤表 ─────────────────────────────

	void DrawSteps(PanelState& s, const Aggregate& agg)
	{
		ImGui::Checkbox("隐藏未执行的 Pass", &s.hideSkipped);
		ImGui::SameLine();
		ImGui::Checkbox("Pass 按 GPU 耗时排序", &s.sortPasses);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"RenderGraph 之外的步骤：墙钟时间，各自提交并等 GPU 做完，就是这一步的完整耗时。\n"
				"SceneRenderGraph：墙钟时间，含等图内 GPU 工作做完；GPU 列是各 Pass GPU 耗时之和。\n"
				"\n"
				"Pass 有两组时间：\n"
				"  当前 / 平均 / 最大 = CPU 侧 Execute 墙钟耗时（取资源 + 录制 + 等待提交 + 提交），不含 GPU 执行；\n"
				"    例外：有回读的 Pass（自动曝光、HZB 遮挡剔除等）要等 GPU 把它和它之前的工作做完，会偏大。\n"
				"  「等待提交」= 等提交锁（前面的 Pass 在补差 / 提交，或有回读的 Pass 拿着锁在等 GPU）；\n"
				"  「CPU 自身」= Execute - 等待提交。\n"
				"  「GPU」= 时间戳：本 Pass 第一条命令开始到最后一条命令结束，含 Pass 内多次提交之间的空闲。\n"
				"图内各 Pass 在 GPU 上按提交顺序串行执行，所以 GPU 时间可以直接相加；占比按 GPU 时间 / SceneRenderGraph。\n"
				"FrameBegin 在图执行前对所有 Pass 并行调用。");

		if (!agg.current)
			return;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		enum Column { ColName = 0, ColCur, ColAvg, ColMax, ColWait, ColSelf, ColGpu, ColFrameBegin, ColShare, ColCount };
		if (!ImGui::BeginTable("##profilerSteps", ColCount, flags))
			return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("步骤", ImGuiTableColumnFlags_WidthStretch, 3.2f);
		ImGui::TableSetupColumn("当前 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("平均 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("最大 ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("等待提交 ms", ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("FrameBegin ms", ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableHeadersRow();

		const double frameAvg = agg.total.Avg();
		const double graphAvg = agg.steps[size_t(RenderProfileStep::SceneGraph)].Avg();

		auto numberCells = [](const Stat& st, bool showCurrent, const ImVec4* color) {
			ImGui::TableSetColumnIndex(ColCur);
			if (!showCurrent)
				ImGui::TextDisabled("—");
			else if (color)
				ImGui::TextColored(*color, "%.2f", Ms(double(st.cur)));
			else
				ImGui::Text("%.2f", Ms(double(st.cur)));
			ImGui::TableSetColumnIndex(ColAvg);
			if (color)
				ImGui::TextColored(*color, "%.2f", Ms(st.Avg()));
			else
				ImGui::Text("%.2f", Ms(st.Avg()));
			ImGui::TableSetColumnIndex(ColMax);
			ImGui::Text("%.2f", Ms(double(st.max)));
			};

		// 总耗时行
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TreeNodeEx("单帧耗时", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
		numberCells(agg.total, true, nullptr);
		ImGui::TableSetColumnIndex(ColShare);
		ImGui::TextDisabled("100%%");

		for (size_t k = 0; k < kStepCount; ++k)
		{
			const auto step = RenderProfileStep(k);
			const Stat& st = agg.steps[k];
			const bool outsideFrame = step == RenderProfileStep::Analysis || step == RenderProfileStep::Queue;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(int(k));
			bool open = false;
			if (step == RenderProfileStep::SceneGraph)
			{
				if (s.scrollToPass)
					ImGui::SetNextItemOpen(true);
				open = ImGui::TreeNodeEx(RenderProfileStepName(step), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen);
			}
			else
			{
				ImGui::TreeNodeEx(RenderProfileStepName(step), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
			}
			if (ImGui::IsItemHovered())
			{
				const char* tip = RenderProfileStepTip(step);
				if (tip || outsideFrame)
					ImGui::SetTooltip("%s%s", tip ? tip : "", outsideFrame ? "\n（发生在渲染循环取走这一帧之前，不计入单帧耗时）" : "");
			}
			ImGui::PopID();

			numberCells(st, true, nullptr);
			if (step == RenderProfileStep::SceneGraph)
			{
				ImGui::TableSetColumnIndex(ColGpu);
				if (agg.hasGpu)
				{
					ImGui::Text("%.2f", Ms(agg.graphGpu.Avg()));
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("图内各 Pass GPU 耗时之和（平均）\n当前 %.2f / 最大 %.2f ms\n和 SceneRenderGraph 墙钟时间的差 ≈ GPU 在图内空闲（等 CPU 提交）的时间",
							Ms(double(agg.graphGpu.cur)), Ms(double(agg.graphGpu.max)));
				}
				else
					ImGui::TextDisabled("—");
			}
			ImGui::TableSetColumnIndex(ColShare);
			if (!outsideFrame && frameAvg > 0.0)
				ImGui::Text("%.1f%%", st.Avg() / frameAvg * 100.0);

			if (step != RenderProfileStep::SceneGraph || !open)
				continue;

			// ── SceneRenderGraph 下的各个 Pass ──
			if (agg.layout)
			{
				const auto& nodes = agg.layout->nodes;
				std::vector<int> rows;
				double maxSelfAvg = 0.0;
				double maxGpuAvg = 0.0;
				for (int i = 0; i < int(nodes.size()); ++i)
				{
					if (nodes[i].fence)
						continue;
					const auto& ps = agg.passes[i];
					if (s.hideSkipped && ps.executedFrames == 0 && i != s.selectedPass)
						continue;
					rows.push_back(i);
					maxSelfAvg = std::max(maxSelfAvg, ps.self.Avg());
					maxGpuAvg = std::max(maxGpuAvg, ps.gpu.Avg());
				}
				if (s.sortPasses)
				{
					std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
						return agg.passes[a].gpu.Avg() > agg.passes[b].gpu.Avg();
						});
				}

				for (int i : rows)
				{
					const auto& node = nodes[i];
					const auto& ps = agg.passes[i];
					const bool skipped = ps.executedFrames == 0;

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(1000 + i);
					const bool selected = s.selectedPass == i;
					if (skipped)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					if (ImGui::Selectable(node.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
						s.selectedPass = selected ? -1 : i;
					if (skipped)
						ImGui::PopStyleColor();
					if (selected && s.scrollToPass)
					{
						ImGui::SetScrollHereY(0.5f);
						s.scrollToPass = false;
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(node.name.c_str());
						ImGui::TextDisabled("batch %d · 执行 %d / %d 帧", node.batch, ps.executedFrames, int(agg.frames.size()));
						if (!node.afters.empty())
						{
							std::string afters;
							for (size_t a = 0; a < node.afters.size() && a < 8; ++a)
							{
								if (a)
									afters += ", ";
								afters += nodes[node.afters[a]].name;
							}
							if (node.afters.size() > 8)
								afters += " ...";
							ImGui::TextDisabled("前驱：%s", afters.c_str());
						}
						ImGui::EndTooltip();
					}
					ImGui::PopID();

					if (skipped)
					{
						for (int c = ColCur; c <= ColGpu; ++c)
						{
							ImGui::TableSetColumnIndex(c);
							ImGui::TextDisabled("未执行");
						}
					}
					else
					{
						numberCells(ps.execute, ps.executedCur, nullptr);
						ImGui::TableSetColumnIndex(ColWait);
						ImGui::TextDisabled("%.2f", Ms(ps.wait.Avg()));
						ImGui::TableSetColumnIndex(ColSelf);
						const ImVec4 selfColor = HeatColor(maxSelfAvg > 0.0 ? float(ps.self.Avg() / maxSelfAvg) : 0.0f);
						ImGui::TextColored(selfColor, "%.2f", Ms(ps.self.Avg()));
						ImGui::TableSetColumnIndex(ColGpu);
						if (ps.gpuFrames > 0)
						{
							const ImVec4 gpuColor = HeatColor(maxGpuAvg > 0.0 ? float(ps.gpu.Avg() / maxGpuAvg) : 0.0f);
							ImGui::TextColored(gpuColor, "%.2f", Ms(ps.gpu.Avg()));
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("GPU  当前 %.2f   平均 %.2f   最大 %.2f ms",
									ps.gpuCur ? Ms(double(ps.gpu.cur)) : 0.0, Ms(ps.gpu.Avg()), Ms(double(ps.gpu.max)));
						}
						else
							ImGui::TextDisabled("—");
					}
					ImGui::TableSetColumnIndex(ColFrameBegin);
					ImGui::Text("%.2f", Ms(ps.frameBegin.Avg()));
					ImGui::TableSetColumnIndex(ColShare);
					if (!skipped && ps.gpuFrames > 0 && graphAvg > 0.0)
						ImGui::Text("%.1f%%", ps.gpu.Avg() / graphAvg * 100.0);
				}
			}
			ImGui::TreePop();
		}

		ImGui::EndTable();
		// 要跳转的行这一帧没画出来（被隐藏等）也不再保持
		s.scrollToPass = false;
	}

	// ───────────────────────────── 节点图 ─────────────────────────────

	void DrawGraph(PanelState& s, const Aggregate& agg)
	{
		if (!agg.layout || agg.layout->nodes.empty())
		{
			ImGui::TextDisabled("还没有 RenderGraph 数据");
			return;
		}

		if (s.geometry.layout != agg.layout.get())
		{
			s.layoutHold = agg.layout;
			BuildGeometry(s.geometry, *agg.layout);
			s.fitRequested = true;
		}

		const auto& layout = *agg.layout;
		const auto& g = s.geometry;
		const int n = int(layout.nodes.size());

		std::vector<double> value(n, 0.0);
		std::vector<char> executed(n, 0);
		double maxValue = 0.0;
		for (int i = 0; i < n; ++i)
		{
			if (layout.nodes[i].fence)
				continue;
			const auto& ps = agg.passes[i];
			const bool current = s.graphValue == ValueMode::Current;
			if (s.graphMetric == GraphMetric::Gpu)
				executed[i] = current ? ps.gpuCur : (ps.gpuFrames > 0);
			else
				executed[i] = current ? ps.executedCur : (ps.executedFrames > 0);
			if (executed[i])
			{
				const Stat& st = s.graphMetric == GraphMetric::Gpu ? ps.gpu
					: (s.graphMetric == GraphMetric::CpuSelf ? ps.self : ps.execute);
				value[i] = Value(st, s.graphValue);
			}
			maxValue = std::max(maxValue, value[i]);
		}

		// 选中节点：只亮它的全部上游（祖先 + 自己），橙色改为「到达它的最长链」，替换整张图的关键路径
		const int selected = (s.selectedPass >= 0 && s.selectedPass < n) ? s.selectedPass : -1;
		std::vector<char> upstream;
		int upstreamPasses = 0;
		if (selected >= 0)
		{
			upstream.assign(n, 0);
			upstream[selected] = 1;
			for (int i = 0; i < n; ++i)
			{
				if (i != selected && g.IsAncestor(i, selected))
				{
					upstream[i] = 1;
					if (!layout.nodes[i].fence)
						++upstreamPasses;
				}
			}
		}

		std::vector<int> parent;
		std::vector<char> onPath;
		double pathLength = 0.0;
		int pathCount = 0;
		CriticalPath(g, layout, value, selected, parent, onPath, pathLength, pathCount);
		const bool showPath = pathLength > 0.0 && (selected >= 0 || s.showCriticalPath);

		// ── 工具栏 ──
		const char* modes[] = { "当前帧", "平均", "最大" };
		int mode = int(s.graphValue);
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::Combo("数值", &mode, modes, IM_ARRAYSIZE(modes)))
			s.graphValue = ValueMode(mode);
		ImGui::SameLine();
		const char* metrics[] = { "GPU", "CPU 自身", "CPU 总耗时" };
		int metric = int(s.graphMetric);
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::Combo("着色", &metric, metrics, IM_ARRAYSIZE(metrics)))
			s.graphMetric = GraphMetric(metric);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"GPU：时间戳计时。图内各 Pass 在 GPU 上按提交顺序串行执行，GPU 着色时看单个 Pass 的耗时，\n"
				"     关键路径只作参考（GPU 上实际是全部相加）。设备不支持时间戳时全部显示未执行。\n"
				"CPU 自身：Execute - 等待提交（录制 / 取资源 / 提交本身的开销）。\n"
				"CPU 总耗时：Execute 墙钟时间，含等提交锁；有回读的 Pass 还含等 GPU。");
		ImGui::SameLine();
		ImGui::Checkbox("关键路径", &s.showCriticalPath);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("没有选中节点时显示整张图的关键路径。\n选中节点后改为显示到达该节点的最长链（不受这个开关影响），点空白处取消选中。");
		ImGui::SameLine();
		if (ImGui::Button("适应窗口"))
			s.fitRequested = true;
		ImGui::SameLine();
		ImGui::TextDisabled("单击节点看上游，右键 / 中键拖动平移，滚轮缩放，双击节点跳到步骤表");
		if (selected >= 0)
		{
			ImGui::Text("到达 %s 的最长链 %.2f ms（%d 个 Pass，含自身）    上游共 %d 个 Pass    SceneRenderGraph %.2f ms",
				layout.nodes[selected].name.c_str(), Ms(pathLength), pathCount, upstreamPasses,
				Ms(Value(agg.steps[size_t(RenderProfileStep::SceneGraph)], s.graphValue)));
		}
		else if (showPath)
		{
			ImGui::Text("关键路径 %.2f ms（%d 个 Pass）    SceneRenderGraph %.2f ms",
				Ms(pathLength), pathCount, Ms(Value(agg.steps[size_t(RenderProfileStep::SceneGraph)], s.graphValue)));
		}

		ImGui::BeginChild("##profilerGraph", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.x = std::max(size.x, 100.0f);
		size.y = std::max(size.y, 100.0f);
		ImGui::InvisibleButton("##graphCanvas", size,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		ImGuiIO& io = ImGui::GetIO();

		if (s.fitRequested)
		{
			const ImVec2 bounds = V2Sub(g.boundsMax, g.boundsMin);
			const float margin = 40.0f;
			const float zx = (size.x - margin * 2.0f) / std::max(bounds.x, 1.0f);
			const float zy = (size.y - margin * 2.0f) / std::max(bounds.y, 1.0f);
			s.zoom = std::clamp(std::min(zx, zy), 0.15f, 1.25f);
			const ImVec2 center = V2Mul(V2Add(g.boundsMin, g.boundsMax), 0.5f);
			s.pan = V2Sub(V2Mul(size, 0.5f), V2Mul(center, s.zoom));
			s.fitRequested = false;
		}

		if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
			s.pan = V2Add(s.pan, io.MouseDelta);
		if (hovered && io.MouseWheel != 0.0f)
		{
			// 以鼠标为中心缩放
			const ImVec2 mouseLocal = V2Sub(io.MousePos, origin);
			const ImVec2 graphPoint = V2Mul(V2Sub(mouseLocal, s.pan), 1.0f / s.zoom);
			s.zoom = std::clamp(s.zoom * std::pow(1.15f, io.MouseWheel), 0.15f, 3.0f);
			s.pan = V2Sub(mouseLocal, V2Mul(graphPoint, s.zoom));
		}

		const float zoom = s.zoom;
		auto toScreen = [&](const ImVec2& p) { return V2Add(V2Add(origin, s.pan), V2Mul(p, zoom)); };

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 canvasMax = V2Add(origin, size);
		dl->AddRectFilled(origin, canvasMax, IM_COL32(24, 26, 30, 255));
		dl->PushClipRect(origin, canvasMax, true);

		// 网格
		const float grid = 48.0f * zoom;
		if (grid > 10.0f)
		{
			const ImU32 gridColor = IM_COL32(255, 255, 255, 10);
			for (float x = std::fmod(s.pan.x, grid); x < size.x; x += grid)
				dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, canvasMax.y), gridColor);
			for (float y = std::fmod(s.pan.y, grid); y < size.y; y += grid)
				dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(canvasMax.x, origin.y + y), gridColor);
		}

		// 边：选中时先画变暗的，再画上游的，免得被盖住
		const float arrow = std::clamp(6.0f * zoom, 3.0f, 9.0f);
		auto drawEdge = [&](int a, int b, bool dim) {
			const ImVec2 p0 = toScreen(ImVec2(g.pos[a].x + g.size[a].x, g.pos[a].y + g.size[a].y * 0.5f));
			const ImVec2 p3 = toScreen(ImVec2(g.pos[b].x, g.pos[b].y + g.size[b].y * 0.5f));
			const bool critical = !dim && showPath && onPath[b] && parent[b] == a;
			ImU32 color;
			if (critical)
				color = IM_COL32(255, 160, 40, 235);
			else if (dim)
				color = IM_COL32(150, 156, 168, 26);
			else if (selected >= 0)
				color = IM_COL32(200, 206, 218, 170);	// 上游的边比平时亮一点
			else
				color = IM_COL32(150, 156, 168, 105);
			const float dx = std::max(20.0f * zoom, (p3.x - p0.x) * 0.5f);
			dl->AddBezierCubic(p0, ImVec2(p0.x + dx, p0.y), ImVec2(p3.x - dx, p3.y), p3, color, critical ? 2.5f : 1.2f);
			dl->AddTriangleFilled(p3, ImVec2(p3.x - arrow, p3.y - arrow * 0.6f), ImVec2(p3.x - arrow, p3.y + arrow * 0.6f), color);
			};
		if (selected < 0)
		{
			for (const auto& [a, b] : g.edges)
				drawEdge(a, b, false);
		}
		else
		{
			// 终点在上游集合里的边，起点必然也在（是它的前驱）
			for (const auto& [a, b] : g.edges)
				if (!upstream[b])
					drawEdge(a, b, true);
			for (const auto& [a, b] : g.edges)
				if (upstream[b])
					drawEdge(a, b, false);
		}

		// 节点
		int hoveredNode = -1;
		ImFont* font = ImGui::GetFont();
		const float baseFont = ImGui::GetFontSize();
		for (int i = 0; i < n; ++i)
		{
			const auto& node = layout.nodes[i];
			const ImVec2 mn = toScreen(g.pos[i]);
			const ImVec2 mx = toScreen(V2Add(g.pos[i], g.size[i]));
			if (mx.x < origin.x || mn.x > canvasMax.x || mx.y < origin.y || mn.y > canvasMax.y)
				continue;

			ImU32 fill;
			if (node.fence)
				fill = IM_COL32(52, 62, 84, 255);
			else if (!executed[i])
				fill = IM_COL32(62, 62, 64, 255);
			else
				fill = ImGui::ColorConvertFloat4ToU32(HeatColor(maxValue > 0.0 ? float(value[i] / maxValue) : 0.0f));

			const bool dim = selected >= 0 && !upstream[i];
			if (dim)
				fill = (fill & ~IM_COL32_A_MASK) | (ImU32(50) << IM_COL32_A_SHIFT);

			const float rounding = 5.0f * zoom;
			dl->AddRectFilled(mn, mx, fill, rounding);
			const bool isSelected = selected == i;
			const bool critical = !dim && showPath && onPath[i] && !node.fence;
			ImU32 border = isSelected ? IM_COL32(255, 255, 255, 255) : (critical ? IM_COL32(255, 160, 40, 255) : IM_COL32(16, 16, 18, 255));
			if (dim)
				border = IM_COL32(16, 16, 18, 60);
			dl->AddRect(mn, mx, border, rounding, 0, (isSelected || critical) ? 2.5f : 1.0f);

			if (hovered && io.MousePos.x >= mn.x && io.MousePos.x <= mx.x && io.MousePos.y >= mn.y && io.MousePos.y <= mx.y)
				hoveredNode = i;

			if (zoom < 0.35f)
				continue;

			ImU32 textColor = (executed[i] || node.fence) ? IM_COL32(245, 245, 245, 255) : IM_COL32(165, 165, 165, 255);
			if (dim)
				textColor = (textColor & ~IM_COL32_A_MASK) | (ImU32(70) << IM_COL32_A_SHIFT);
			const float available = (mx.x - mn.x) - 8.0f * zoom;
			const char* name = node.name.c_str();
			float nameSize = baseFont * zoom * (node.fence ? 0.85f : 1.0f);
			ImVec2 nameExtent = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name);
			if (nameExtent.x > available && nameExtent.x > 0.0f)
			{
				nameSize *= available / nameExtent.x;
				nameExtent = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name);
			}

			if (node.fence)
			{
				dl->AddText(font, nameSize, ImVec2(mn.x + ((mx.x - mn.x) - nameExtent.x) * 0.5f, mn.y + ((mx.y - mn.y) - nameExtent.y) * 0.5f),
					textColor, name);
				continue;
			}

			dl->AddText(font, nameSize, ImVec2(mn.x + ((mx.x - mn.x) - nameExtent.x) * 0.5f, mn.y + 4.0f * zoom), textColor, name);

			char text[48];
			if (executed[i])
				std::snprintf(text, sizeof(text), "%.2f ms", Ms(value[i]));
			else
				std::snprintf(text, sizeof(text), "未执行");
			const float valueSize = baseFont * zoom * 0.95f;
			const ImVec2 valueExtent = font->CalcTextSizeA(valueSize, FLT_MAX, 0.0f, text);
			dl->AddText(font, valueSize, ImVec2(mn.x + ((mx.x - mn.x) - valueExtent.x) * 0.5f, mx.y - valueExtent.y - 4.0f * zoom),
				textColor, text);
		}

		dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(200, 200, 200, 170),
			selected >= 0
			? "选中：只亮它的上游  橙：到达它的最长链  白框：选中  点空白处取消"
			: "颜色 = 耗时（绿→红）  灰：未执行  蓝灰：栅栏  橙：关键路径  白框：选中");
		dl->PopClipRect();

		// 交互
		if (hoveredNode >= 0)
		{
			const auto& node = layout.nodes[hoveredNode];
			const auto& ps = agg.passes[hoveredNode];
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(node.name.c_str());
			if (node.fence)
				ImGui::TextDisabled("栅栏节点（只用于排序，不执行）");
			else if (ps.executedFrames == 0)
				ImGui::TextDisabled("统计范围内没有执行");
			else
			{
				if (ps.gpuFrames > 0)
					ImGui::Text("GPU        当前 %.2f   平均 %.2f   最大 %.2f ms",
						ps.gpuCur ? Ms(double(ps.gpu.cur)) : 0.0, Ms(ps.gpu.Avg()), Ms(double(ps.gpu.max)));
				else
					ImGui::TextDisabled("GPU        无计时");
				ImGui::Text("CPU 总耗时 当前 %.2f   平均 %.2f   最大 %.2f ms",
					ps.executedCur ? Ms(double(ps.execute.cur)) : 0.0, Ms(ps.execute.Avg()), Ms(double(ps.execute.max)));
				ImGui::Text("CPU   当前 %.2f   平均 %.2f   最大 %.2f ms",
					ps.executedCur ? Ms(double(ps.self.cur)) : 0.0, Ms(ps.self.Avg()), Ms(double(ps.self.max)));
				ImGui::Text("等待提交 平均 %.2f ms    FrameBegin 平均 %.2f ms", Ms(ps.wait.Avg()), Ms(ps.frameBegin.Avg()));
				ImGui::TextDisabled("batch %d · 层 %d · 执行 %d / %d 帧", node.batch, g.layer[hoveredNode], ps.executedFrames, int(agg.frames.size()));
			}
			ImGui::EndTooltip();

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				s.selectedPass = hoveredNode;
				s.jumpToSteps = true;
				s.scrollToPass = true;
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				s.selectedPass = hoveredNode;
		}
		else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			s.selectedPass = -1;

		ImGui::EndChild();
	}
}

namespace RenderProfilerPanel
{
	bool& Visible()
	{
		return State().visible;
	}

	void Draw(EditorApp* app)
	{
		auto& s = State();
		auto channel = Channel(app);

		// 窗口关着或暂停时关闭统计：渲染器那边各打点处只剩一次判空
		if (channel)
			channel->SetEnabled(s.visible && !s.paused);

		if (!s.visible)
		{
			if (!s.frames.empty())
				std::vector<ProfilePtr>().swap(s.frames);
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(860.0f, 620.0f), ImGuiCond_FirstUseEver);
		// 浮动窗口，不参与停靠
		if (!ImGui::Begin("渲染性能", &s.visible, ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		if (!channel)
		{
			ImGui::TextDisabled("没有可用的 3D 渲染器");
			ImGui::End();
			return;
		}

		if (!s.paused)
		{
			s.frames = channel->Snapshot();
			s.viewFrame = -1;
		}

		const Aggregate agg = BuildAggregate(s);
		DrawHeader(s, agg, *channel);

		if (ImGui::BeginTabBar("##profilerTabs"))
		{
			const ImGuiTabItemFlags stepsFlags = s.jumpToSteps ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem("步骤", nullptr, stepsFlags))
			{
				s.jumpToSteps = false;
				DrawSteps(s, agg);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("RenderGraph"))
			{
				DrawGraph(s, agg);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
