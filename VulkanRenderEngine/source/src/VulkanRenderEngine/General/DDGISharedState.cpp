#include "vkstdafx.h"
#include "VulkanRenderEngine/General/DDGIDef.h"
#include "VulkanRenderEngine/General/RenderState.h"

#include <cstring>
#include <limits>
#include <unordered_set>

namespace
{
	constexpr uint32_t kInvalid = 0xFFFFFFFFu;
	// 手动体积按到相机的距离降低更新频率：这个距离以内按满频率算
	constexpr float kImportanceDistance = 16.0f;

	glm::uvec3 ClampCounts(const glm::uvec3& c)
	{
		return glm::clamp(c, glm::uvec3(1u), glm::uvec3(DDGIAtlasLayout::MaxAxisCount));
	}

	float DistanceToBox(const glm::vec3& p, const glm::vec3& mn, const glm::vec3& mx)
	{
		const glm::vec3 d = glm::max(glm::max(mn - p, glm::vec3(0.0f)), p - mx);
		return glm::length(d);
	}

	uint32_t FloatBits(float f)
	{
		uint32_t u = 0;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

	float BitsFloat(uint32_t u)
	{
		float f = 0.0f;
		std::memcpy(&f, &u, sizeof(f));
		return f;
	}

	// Allocation::originCell 的特殊值：本帧新分配，还没有历史
	const glm::ivec3 kNewAllocation(std::numeric_limits<int>::min());

	std::shared_ptr<StorageBlock> MakeBuffer(const void* data, size_t size)
	{
		return DDGIMakeHostBlock<StorageBlock>(data, size);
	}

	// 规则轮转：相邻探针相位依次错开（同旧版按下标取模），空间上均匀、同一帧更新的探针更集中
	uint32_t PhaseOf(int32_t key, uint32_t period)
	{
		const int32_t p = int32_t(period);
		return uint32_t(((key % p) + p) % p);
	}
}

DDGISharedState::DDGISharedState(std::shared_ptr<DDGIDebugChannel> channel)
	: _channel(std::move(channel))
{}

std::shared_ptr<const DDGIFrame> DDGISharedState::Resolve(const RenderState& state)
{
	std::lock_guard lock(_mutex);

	const uint32_t frameIndex = state.renderRecord.frameIndex;
	for (const auto& cached : _recent)
	{
		if (cached && cached->frameIndex == frameIndex)
			return cached;
	}

	auto frame = std::make_shared<DDGIFrame>();
	frame->frameIndex = frameIndex;
	if (_channel)
		frame->debug = _channel->GetSettings();

	// 乱序到达的旧帧（正常不会发生）：不输出，也不推进状态
	if (_hasLast && frameIndex <= _lastFrame)
		return frame;

	const auto& p = state.option.ddgiParams;
	const uint32_t budget = std::clamp(p.updateBudget, 1u, DDGIAtlasLayout::Get().budget);

	// 帧号不连续说明 DDGI 中途关闭过（或有帧没走到这里），历史数据不可信
	frame->historyValid = _hasLast && frameIndex == _lastFrame + 1;

	std::vector<DDGIResolvedVolume> dropped;
	BuildVolumes(state, *frame, dropped);
	AssignPeriods(state, *frame, budget);

	// probeInfo 只装手动编辑与选中：槽位布局、编辑表、选择没变时复用上一份。
	// 编辑和选择都按世界格子记，槽位又按世界格子滚动寻址，所以只有「有编辑或被选中」的体积才需要把位置算进签名；
	// 级联没有编辑，相机跨格平移不触发重建（被覆盖与否每帧按体积表现算，见 BuildCoveredRanges）
	std::vector<uint64_t> signature;
	signature.reserve(frame->volumes.size() * 8 + 4);
	for (const auto& v : frame->volumes)
	{
		const bool hasEdits = v.edits && !v.edits->empty();
		const bool positionMatters = hasEdits || (frame->debug.selectedVolumeId != 0 && v.id == frame->debug.selectedVolumeId);
		signature.insert(signature.end(), {
			uint64_t(v.id), uint64_t(v.base),
			uint64_t(v.counts.x), uint64_t(v.counts.y), uint64_t(v.counts.z),
			uint64_t(reinterpret_cast<uintptr_t>(v.edits.get())) });
		if (positionMatters)
		{
			signature.insert(signature.end(), {
				uint64_t(uint32_t(v.originCell.x)), uint64_t(uint32_t(v.originCell.y)), uint64_t(uint32_t(v.originCell.z)),
				uint64_t(FloatBits(v.spacing.x)), uint64_t(FloatBits(v.spacing.y)), uint64_t(FloatBits(v.spacing.z)) });
		}
	}
	signature.push_back(frame->debug.selectedVolumeId);
	signature.push_back(frame->debug.selectionVersion);
	signature.push_back(frame->totalProbes);

	bool probeCacheRebuilt = false;
	if (!_cache.valid || _cache.signature != signature)
	{
		RebuildProbeCache(*frame);
		_cache.signature = std::move(signature);
		_cache.valid = true;
		probeCacheRebuilt = true;
	}
	frame->probeInfo = _cache.probeInfo;

	BuildCoveredRanges(*frame);
	BuildResetAndUpdateLists(*frame, budget, probeCacheRebuilt);

	// 烘焙请求：渲染器在这一帧把该体积的 probeData 拷到回读缓冲（见 DDGIBlendPass）
	if (_channel)
	{
		uint32_t bakeVolumeId = 0;
		uint64_t bakeRequestId = 0;
		if (_channel->TakeBakeRequest(bakeVolumeId, bakeRequestId))
		{
			for (uint32_t i = 0; i < uint32_t(frame->volumes.size()); ++i)
			{
				if (frame->volumes[i].id == bakeVolumeId)
				{
					frame->bakeActive = true;
					frame->bakeVolumeIndex = i;
					frame->bakeRequestId = bakeRequestId;
					break;
				}
			}
			if (!frame->bakeActive)
			{
				DDGIBakeResult empty;
				empty.requestId = bakeRequestId;
				empty.volumeId = bakeVolumeId;
				_channel->PublishBake(std::move(empty));
			}
		}
	}

	frame->enabled = !frame->volumes.empty() && frame->totalProbes > 0;
	UploadGPUData(*frame, state);
	PublishStats(*frame, budget, dropped);

	_hasLast = true;
	_lastFrame = frameIndex;

	_recent[_next] = frame;
	_next = (_next + 1) % uint32_t(_recent.size());
	return frame;
}

void DDGISharedState::BuildVolumes(const RenderState& state, DDGIFrame& frame, std::vector<DDGIResolvedVolume>& dropped)
{
	const auto& p = state.option.ddgiParams;
	const glm::vec3 cameraPos = state.camera.position;

	std::vector<DDGIResolvedVolume> candidates;

	// 级联：以相机为中心，每级最小角吸附到本级间距的世界网格；相机跨格时整格平移，历史按世界格子找回
	if (p.cascadeEnable)
	{
		const uint32_t levels = std::clamp(p.cascadeLevels, 1u, DDGIAtlasLayout::MaxCascadeLevels);
		const glm::uvec3 counts = ClampCounts(p.cascadeCounts);
		const glm::vec3 baseSpacing = glm::max(p.cascadeBaseSpacing, glm::vec3(0.25f));
		for (uint32_t level = 0; level < levels; ++level)
		{
			DDGIResolvedVolume v;
			v.id = DDGICascadeVolumeId(level);
			v.cascade = true;
			v.level = level;
			v.spacing = baseSpacing * float(1u << level);
			v.counts = counts;
			v.originCell = glm::ivec3(glm::floor((cameraPos - glm::vec3(counts) * v.spacing * 0.5f) / v.spacing));
			v.priority = -1.0f - float(level);			// 低于所有手动体积（手动优先级 >= 0）
			v.fadeCells = std::max(0.01f, p.cascadeFadeCells);
			v.autoRelocate = p.cascadeAutoRelocate;
			v.importance = 1.0f / float(1u << level);
			candidates.push_back(std::move(v));
		}
	}

	for (const auto& desc : state.ddgi.volumes)
	{
		if (desc.id >= DDGICascadeVolumeId(0))
			continue;

		DDGIResolvedVolume v;
		v.id = desc.id;
		v.spacing = glm::max(desc.spacing, glm::vec3(0.25f));
		v.counts = ClampCounts(desc.counts);
		v.originCell = DDGIVolumeOriginCell(desc.center, v.spacing, v.counts);
		v.priority = float(std::max(0, desc.priority));
		v.fadeCells = std::max(0.01f, desc.fadeCells);
		v.autoRelocate = desc.autoRelocate;
		v.edits = desc.edits;
		candidates.push_back(std::move(v));
	}

	for (auto& v : candidates)
		v.probeCount = v.counts.x * v.counts.y * v.counts.z;

	// 采样顺序 = 表顺序：优先级高的在前；同优先级时密的在前
	std::stable_sort(candidates.begin(), candidates.end(), [](const DDGIResolvedVolume& a, const DDGIResolvedVolume& b) {
		if (a.priority != b.priority)
			return a.priority > b.priority;
		const float sa = std::min(a.spacing.x, std::min(a.spacing.y, a.spacing.z));
		const float sb = std::min(b.spacing.x, std::min(b.spacing.y, b.spacing.z));
		if (sa != sb)
			return sa < sb;
		return a.id < b.id;
		});

	AllocateVolumes(frame, candidates, dropped);
}

// 常驻槽位分配。图集原地更新，所以体积的 base 不能像以前那样每帧按排序重排：
//   * 体积还在、counts 与 spacing 没变 → 沿用原来的区间（历史保留）；
//   * 新体积、或 counts / spacing 变了 → 首次适配（first-fit）找空位，整体重置；
//   * 不再出现的体积释放区间；
//   * 放不下时整理一次：全部按优先级从 0 开始重排（搬动的体积整体重置），仍放不下的按优先级从低到高停用。
// candidates 已按采样优先级排好序，frame.volumes 保持这个顺序（与 base 无关）
void DDGISharedState::AllocateVolumes(DDGIFrame& frame, std::vector<DDGIResolvedVolume>& candidates, std::vector<DDGIResolvedVolume>& dropped)
{
	const uint32_t capacity = DDGIAtlasLayout::Get().capacity;

	// 释放这一帧没出现的体积
	{
		std::unordered_set<uint32_t> present;
		for (const auto& v : candidates)
			present.insert(v.id);
		for (auto it = _allocations.begin(); it != _allocations.end();)
		{
			if (!present.contains(it->first))
				it = _allocations.erase(it);
			else
				++it;
		}
	}

	auto sameLayout = [](const Allocation& a, const DDGIResolvedVolume& v) {
		return a.counts == v.counts && a.spacing == v.spacing;
		};

	// counts / spacing 变了的体积先释放，再统一分配
	for (const auto& v : candidates)
	{
		auto it = _allocations.find(v.id);
		if (it != _allocations.end() && !sameLayout(it->second, v))
			_allocations.erase(it);
	}

	auto firstFit = [&](uint32_t count, uint32_t& outBase) -> bool {
		std::vector<std::pair<uint32_t, uint32_t>> used;
		used.reserve(_allocations.size());
		for (const auto& [id, a] : _allocations)
			used.emplace_back(a.base, a.base + a.count);
		std::sort(used.begin(), used.end());
		uint32_t cursor = 0;
		for (const auto& [b, e] : used)
		{
			if (uint64_t(cursor) + count <= b)
				break;
			cursor = std::max(cursor, e);
		}
		if (uint64_t(cursor) + count > capacity)
			return false;
		outBase = cursor;
		return true;
		};

	const size_t maxVolumes = std::min<size_t>(candidates.size(), DDGIAtlasLayout::MaxVolumes);
	for (size_t i = maxVolumes; i < candidates.size(); ++i)
		_allocations.erase(candidates[i].id);		// 超出体积数上限的停用，不占槽位
	bool fits = true;
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		auto& v = candidates[i];
		if (i >= maxVolumes)
			break;
		if (_allocations.contains(v.id))
			continue;
		uint32_t base = 0;
		if (!firstFit(v.probeCount, base))
		{
			fits = false;
			break;
		}
		_allocations[v.id] = Allocation{ base, v.probeCount, v.counts, v.spacing, kNewAllocation };
	}

	if (!fits)
	{
		// 整理：全部按优先级从 0 开始重排，所有体积整体重置
		_allocations.clear();
		uint32_t cursor = 0;
		for (size_t i = 0; i < maxVolumes; ++i)
		{
			auto& v = candidates[i];
			if (uint64_t(cursor) + v.probeCount > capacity)
				continue;
			_allocations[v.id] = Allocation{ cursor, v.probeCount, v.counts, v.spacing, kNewAllocation };
			cursor += v.probeCount;
		}
	}

	frame.totalProbes = 0;
	frame.usedProbes = 0;
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		auto& v = candidates[i];
		auto it = (i < maxVolumes) ? _allocations.find(v.id) : _allocations.end();
		if (it == _allocations.end())
		{
			dropped.push_back(std::move(v));
			continue;
		}

		Allocation& a = it->second;
		v.base = a.base;
		const bool newlyAllocated = a.originCell == kNewAllocation;
		v.hasHistory = frame.historyValid && !newlyAllocated;
		v.prevOriginCell = a.originCell;
		a.originCell = v.originCell;

		frame.totalProbes = std::max(frame.totalProbes, a.base + a.count);
		frame.usedProbes += v.probeCount;
		frame.volumes.push_back(std::move(v));
	}
}

void DDGISharedState::AssignPeriods(const RenderState& state, DDGIFrame& frame, uint32_t budget)
{
	const glm::vec3 cameraPos = state.camera.position;

	double total = 0.0;
	double weighted = 0.0;
	for (auto& v : frame.volumes)
	{
		if (!v.cascade)
		{
			const glm::vec3 mn = v.Origin();
			const float d = DistanceToBox(cameraPos, mn, mn + v.Extent());
			v.importance = 1.0f / std::max(1.0f, d / kImportanceDistance);
		}
		total += v.probeCount;
		weighted += double(v.probeCount) * v.importance;
	}

	// 全部装得下就每帧全量更新；否则按重要度分配，Σ count / period ≈ 预算
	const double scale = (total <= budget || weighted <= 0.0) ? 0.0 : double(budget) / weighted;
	for (auto& v : frame.volumes)
	{
		if (scale <= 0.0)
			v.period = 1;
		else
			v.period = std::max(1u, uint32_t(std::ceil(1.0 / (scale * v.importance))));
		v.phase = frame.frameIndex % v.period;
	}
}

void DDGISharedState::RebuildProbeCache(const DDGIFrame& frame)
{
	const uint32_t total = frame.totalProbes;
	std::vector<glm::vec4> info(total, glm::vec4(0.0f));
	_cache.disabled.assign(total, 0);
	_cache.disabledSlots.clear();

	std::unordered_set<int64_t> selected;
	if (frame.debug.selectedVolumeId != 0 && frame.debug.selectedCells)
	{
		for (const auto& c : *frame.debug.selectedCells)
			selected.insert(DDGIPackCell(c));
	}

	for (size_t i = 0; i < frame.volumes.size(); ++i)
	{
		const auto& v = frame.volumes[i];
		const bool hasEdits = v.edits && !v.edits->empty();
		const bool isSelectedVolume = !selected.empty() && v.id == frame.debug.selectedVolumeId;
		if (!hasEdits && !isSelectedVolume)
			continue;							// 没有编辑也没被选中：整段保持 0（级联总是走这里）

		const glm::vec3 offsetLimit = v.spacing * 0.45f;

		for (uint32_t z = 0; z < v.counts.z; ++z)
		for (uint32_t y = 0; y < v.counts.y; ++y)
		for (uint32_t x = 0; x < v.counts.x; ++x)
		{
			const glm::ivec3 local(x, y, z);
			const glm::ivec3 cell = v.originCell + local;
			const uint32_t slot = DDGISlotOf(v.base, v.originCell, v.counts, local);
			const int64_t key = DDGIPackCell(cell);

			uint32_t flags = 0;
			glm::vec3 offset(0.0f);

			if (hasEdits)
			{
				auto it = v.edits->find(key);
				if (it != v.edits->end())
				{
					if (it->second.disabled)
						flags |= DDGIAtlasLayout::ProbeFlagDisabled;
					if (it->second.locked)
					{
						flags |= DDGIAtlasLayout::ProbeFlagLocked;
						offset = glm::clamp(it->second.offset, -offsetLimit, offsetLimit);
					}
				}
			}

			if (isSelectedVolume && selected.contains(key))
				flags |= DDGIAtlasLayout::ProbeFlagSelected;

			info[slot] = glm::vec4(offset, BitsFloat(flags));
			if (flags & DDGIAtlasLayout::ProbeFlagDisabled)
			{
				_cache.disabled[slot] = 1;
				_cache.disabledSlots.push_back(slot);
			}
		}
	}

	_cache.probeInfo = MakeBuffer(info.data(), info.size() * sizeof(glm::vec4));
}

// 被更高优先级体积完全覆盖：该体积在这一带的权重恒为 1，本体积的探针用不到，不更新、不参与采样。
// 判定（与 shader 的 ddgiProbeCovered 逐项一致，改一边必须改另一边）：
//   探针格子中心 pos = origin_i + (local + 0.5) · spacing_i
//   余量 margin = fadeCells_j · spacing_j + 1.5 · spacing_i（对方的过渡带 + 本体积三线性插值的一格与采样偏移）
//   origin_j + margin < pos < origin_j + counts_j · spacing_j − margin（三个轴都满足）
// 每个轴独立，满足条件的下标是一段连续区间，逐轴扫一遍即可（每轴最多 255 个）
void DDGISharedState::BuildCoveredRanges(const DDGIFrame& frame)
{
	const size_t count = frame.volumes.size();
	_covered.resize(count);
	for (size_t i = 0; i < count; ++i)
	{
		auto& ranges = _covered[i];
		ranges.clear();

		const auto& v = frame.volumes[i];
		const glm::vec3 origin = v.Origin();
		for (size_t j = 0; j < i; ++j)
		{
			const auto& h = frame.volumes[j];
			const glm::vec3 hOrigin = h.Origin();
			const glm::vec3 margin = h.fadeCells * h.spacing + 1.5f * v.spacing;
			const glm::vec3 mn = hOrigin + margin;
			const glm::vec3 mx = hOrigin + glm::vec3(h.counts) * h.spacing - margin;

			CoveredRange range{ glm::ivec3(0), glm::ivec3(-1) };
			bool empty = false;
			for (int axis = 0; axis < 3 && !empty; ++axis)
			{
				int lo = -1, hi = -2;
				for (uint32_t k = 0; k < v.counts[axis]; ++k)
				{
					const float pos = origin[axis] + (float(k) + 0.5f) * v.spacing[axis];
					if (pos > mn[axis] && pos < mx[axis])
					{
						if (lo < 0)
							lo = int(k);
						hi = int(k);
					}
				}
				if (lo < 0)
					empty = true;
				else
				{
					range.lo[axis] = lo;
					range.hi[axis] = hi;
				}
			}
			if (!empty)
				ranges.push_back(range);
		}
	}
}

bool DDGISharedState::IsCovered(size_t volumeIndex, const glm::ivec3& local) const
{
	if (volumeIndex >= _covered.size())
		return false;
	for (const auto& r : _covered[volumeIndex])
	{
		if (glm::all(glm::greaterThanEqual(local, r.lo)) && glm::all(glm::lessThanEqual(local, r.hi)))
			return true;
	}
	return false;
}

// 每帧两张表：
//   重置表：要在探针更新之前清零的槽位（DDGIReset.comp）——整体重置的体积、平移后新进入的格子、
//           以及编辑表变化时所有被禁用的探针（保证它们 n = 0，不参与采样）；
//   更新表：本帧追踪 + 混合的探针。先放新出现（刚重置）的格子，再按轮转相位放，直到预算用完。
// 两者都只遍历要处理的格子：新进入的格子按平移量逐行求出，轮转按「同一行里满足相位的 x 是等差数列」跳着取，
// 不再逐个扫全部槽位。
void DDGISharedState::BuildResetAndUpdateLists(DDGIFrame& frame, uint32_t budget, bool probeCacheRebuilt)
{
	const uint32_t total = frame.totalProbes;
	const uint32_t stamp = frame.frameIndex + 1u;
	if (_updateStamp.size() < total)
		_updateStamp.resize(total, 0u);

	auto& resetList = _resetList;
	auto& list = _updateList;
	resetList.assign(1, 0u);
	list.clear();

	bool full = false;
	auto add = [&](uint32_t slot) {
		if (list.size() >= budget)
		{
			full = true;
			return;
		}
		if (_updateStamp[slot] == stamp)
			return;
		_updateStamp[slot] = stamp;
		list.push_back(slot);
		};
	auto eligible = [&](size_t vi, uint32_t slot, const glm::ivec3& local) {
		if (slot < _cache.disabled.size() && _cache.disabled[slot])
			return false;
		return !IsCovered(vi, local);
		};

	// 1) 重置 + 新格子优先更新
	for (size_t vi = 0; vi < frame.volumes.size(); ++vi)
	{
		const auto& v = frame.volumes[vi];
		const glm::ivec3 counts(v.counts);

		auto visit = [&](const glm::ivec3& local) {
			const uint32_t slot = DDGISlotOf(v.base, v.originCell, v.counts, local);
			resetList.push_back(slot);
			if (!full && eligible(vi, slot, local))
				add(slot);
			};

		const glm::ivec3 shift = v.originCell - v.prevOriginCell;
		const bool whole = !v.hasHistory || glm::any(glm::greaterThanEqual(glm::abs(shift), counts));
		if (whole)
		{
			for (int z = 0; z < counts.z; ++z)
			for (int y = 0; y < counts.y; ++y)
			for (int x = 0; x < counts.x; ++x)
				visit(glm::ivec3(x, y, z));
			continue;
		}
		if (shift == glm::ivec3(0))
			continue;

		// 局部格子 c 在上一帧的局部坐标是 c + shift；落在上一帧范围外的就是新进入的
		auto entering = [&](int axis, int c) { const int p = c + shift[axis]; return p < 0 || p >= counts[axis]; };
		const int xFirst = shift.x < 0 ? 0 : counts.x - shift.x;	// 一行里新进入的 x 是一段：[xFirst, xLast)
		const int xLast = shift.x < 0 ? -shift.x : counts.x;
		for (int z = 0; z < counts.z; ++z)
		for (int y = 0; y < counts.y; ++y)
		{
			if (entering(1, y) || entering(2, z))
			{
				for (int x = 0; x < counts.x; ++x)
					visit(glm::ivec3(x, y, z));
			}
			else if (shift.x != 0)
			{
				for (int x = xFirst; x < xLast; ++x)
					visit(glm::ivec3(x, y, z));
			}
		}
	}

	// 编辑表变化：被禁用的探针统一清零（之后它们不再更新，一直保持 n = 0）
	if (probeCacheRebuilt)
		resetList.insert(resetList.end(), _cache.disabledSlots.begin(), _cache.disabledSlots.end());

	// 2) 轮转：重要度高的体积先排；相位键 = 世界格子 x + 3y + 7z
	if (!full)
	{
		std::vector<size_t> order(frame.volumes.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return frame.volumes[a].importance > frame.volumes[b].importance;
			});

		for (size_t idx : order)
		{
			if (full)
				break;
			const auto& v = frame.volumes[idx];
			const int period = int(std::max(1u, v.period));
			const glm::ivec3 counts(v.counts);
			for (int z = 0; z < counts.z && !full; ++z)
			for (int y = 0; y < counts.y && !full; ++y)
			{
				// (oc.x + x) + 3·wy + 7·wz ≡ phase (mod period) → x ≡ phase - oc.x - 3·wy - 7·wz
				const int wy = v.originCell.y + y;
				const int wz = v.originCell.z + z;
				const int x0 = int(PhaseOf(int32_t(v.phase) - v.originCell.x - 3 * wy - 7 * wz, uint32_t(period)));
				for (int x = x0; x < counts.x; x += period)
				{
					const glm::ivec3 local(x, y, z);
					const uint32_t slot = DDGISlotOf(v.base, v.originCell, v.counts, local);
					if (!eligible(idx, slot, local))
						continue;
					add(slot);
					if (full)
						break;
				}
			}
		}
	}

	resetList[0] = uint32_t(resetList.size() - 1);
	frame.resetCount = resetList[0];
	frame.updateCount = uint32_t(list.size());
	frame.updateList = Upload(_ringUpdateList, list.data(), list.size() * sizeof(uint32_t));
	frame.resetList = Upload(_ringResetList, resetList.data(), resetList.size() * sizeof(uint32_t));
}

std::shared_ptr<StorageBlock> DDGISharedState::Upload(UploadRing& ring, const void* data, size_t size)
{
	auto& block = ring.blocks[ring.next];
	ring.next = (ring.next + 1) % uint32_t(ring.blocks.size());

	const uint64_t need = std::max<uint64_t>(size, 16);
	if (!block || block->GetSize() < need)
	{
		// 按 1.5 倍预留，避免每次涨一点就重建
		block = DDGIMakeHostBlock<StorageBlock>(nullptr, size_t(need + need / 2));
	}
	if (data && size > 0)
		block->WriteData(data, size);
	return block;
}

void DDGISharedState::UploadGPUData(DDGIFrame& frame, const RenderState& state)
{
	const auto& p = state.option.ddgiParams;
	const auto& debug = frame.debug;

	std::vector<DDGIVolumeGPU> table;
	table.reserve(frame.volumes.size());
	uint32_t drawVolumeIndex = kInvalid;
	for (uint32_t i = 0; i < uint32_t(frame.volumes.size()); ++i)
	{
		const auto& v = frame.volumes[i];
		uint32_t flags = 0;
		if (v.cascade) flags |= DDGIAtlasLayout::VolumeFlagCascade;
		if (v.autoRelocate) flags |= DDGIAtlasLayout::VolumeFlagAutoRelocate;
		if (v.hasHistory) flags |= DDGIAtlasLayout::VolumeFlagHistory;

		DDGIVolumeGPU g{};
		g.origin = glm::vec4(v.Origin(), v.fadeCells);
		g.spacing = glm::vec4(v.spacing, v.priority);
		g.counts = glm::uvec4(v.counts, v.base);
		g.originCell = glm::ivec4(v.originCell, 0);
		g.state = glm::uvec4(flags, 0u, 0u, 0u);
		g.info = glm::uvec4(v.id, v.period, v.phase, v.level);
		g.reserved = glm::vec4(0.0f);
		table.push_back(g);

		if (debug.onlySelectedVolume && v.id == debug.selectedVolumeId)
			drawVolumeIndex = i;
	}
	frame.volumeTable = Upload(_ringVolumeTable, table.data(), table.size() * sizeof(DDGIVolumeGPU));

	const uint32_t width = std::max(1u, state.framebuffer.width);
	const uint32_t height = std::max(1u, state.framebuffer.height);
	const glm::uvec2 leakPixel(
		std::min(uint32_t(std::max(0.0f, debug.leakPixelUV.x) * width), width - 1),
		std::min(uint32_t(std::max(0.0f, debug.leakPixelUV.y) * height), height - 1));
	const bool leak = debug.leakDebug && debug.leakPixelValid;

	DDGIUniforms& u = frame.uniforms;
	u.shadingParams = glm::vec4(std::max(0.0f, p.normalBias), std::max(0.0f, p.viewBias), std::max(0.0f, p.energy), std::max(0.0f, p.skyIntensity));
	u.filterParams = glm::vec4(
		std::max(0.001f, p.backfaceThreshold),
		1.0f - std::clamp(p.hysteresis, 0.0f, 0.98f),
		std::max(0.0f, p.lightChangeThreshold),
		std::max(0.1f, p.maxRayDistance));
	u.drawParams = glm::vec4(std::clamp(debug.probeRadius, 0.01f, 1.0f), 0.0f, 0.0f, 0.0f);
	u.counts = glm::uvec4(uint32_t(frame.volumes.size()), frame.totalProbes, frame.updateCount, frame.frameIndex % 100000u);
	u.flags = glm::uvec4(frame.historyValid ? 1u : 0u, 0u, frame.bakeActive ? frame.bakeVolumeIndex : kInvalid, leak ? 1u : 0u);
	u.debug = glm::uvec4(leakPixel.x, leakPixel.y, uint32_t(debug.drawMode), debug.onlySelectedVolume ? drawVolumeIndex : kInvalid);
	u.cameraPos = glm::vec4(state.camera.position, 1.0f);
	if (!frame.enabled)
		u.counts.x = 0;		// Apply 输出全零（combin 是加法合成）

	frame.uniformBlock = DDGIMakeHostBlock<UniformBlock>(&u, sizeof(u));
}

void DDGISharedState::PublishStats(const DDGIFrame& frame, uint32_t budget, const std::vector<DDGIResolvedVolume>& dropped)
{
	if (!_channel)
		return;

	DDGIRuntimeStats stats;
	stats.frameIndex = frame.frameIndex;
	stats.capacity = DDGIAtlasLayout::Get().capacity;
	stats.budget = budget;
	stats.usedProbes = frame.usedProbes;
	stats.updatedProbes = frame.updateCount;

	auto toInfo = [](const DDGIResolvedVolume& v, bool isDropped) {
		DDGIVolumeRuntimeInfo info;
		info.id = v.id;
		info.cascade = v.cascade;
		info.level = v.level;
		info.priority = int(v.priority);
		info.origin = v.Origin();
		info.spacing = v.spacing;
		info.counts = v.counts;
		info.base = v.base;
		info.probeCount = v.probeCount;
		info.updatePeriod = v.period;
		info.dropped = isDropped;
		return info;
		};

	for (const auto& v : frame.volumes)
		stats.volumes.push_back(toInfo(v, false));
	for (const auto& v : dropped)
		stats.volumes.push_back(toInfo(v, true));

	_channel->PublishStats(std::move(stats));
}
