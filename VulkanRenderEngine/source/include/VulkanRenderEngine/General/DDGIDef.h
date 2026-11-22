#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>
#include <mutex>
#include <glm/glm.hpp>
#include "VulkanRenderEngine/RenderOption.h"

// DDGI 公共定义：图集布局、shader UBO、探针体积解析、跨帧共享状态。
//
// 坐标约定（C++ 与 shader 一致）：
//   探针世界位置 = volumeOrigin + (逻辑坐标 + 0.5) * spacing
//   图集槽位     = (逻辑坐标 + scrollOffset) % counts，图集按槽位的线性下标存储
//
// 资源：
//   rayData     每探针 RayGrid² 条光线；rgb = 辐亮度，a = 命中距离（负值 = 背面命中）
//   irradiance  每探针 IrradianceInner² + 1px 边框；rgb = 余弦卷积后的 E/π，a = 背面命中比例
//   depth       每探针 DepthInner² + 1px 边框；rg = (E[t], E[t²])
//   probeState  每探针 1 texel；累积次数 n 与光照突变检测的统计量（见 DDGIBlend.comp）

// ───────────────────────────── 图集布局 ─────────────────────────────

struct DDGIAtlasLayout
{
	static constexpr uint32_t RayGridSize = 16;				// 每探针 16×16 = 256 条光线
	static constexpr uint32_t RaysPerProbe = RayGridSize * RayGridSize;

	static constexpr uint32_t IrradianceInnerSize = 8;		// 余弦卷积后是低频量，8×8 足够
	static constexpr uint32_t IrradianceTileSize = IrradianceInnerSize + 2;
	static constexpr uint32_t DepthInnerSize = 16;			// 可见性需要更高的角分辨率
	static constexpr uint32_t DepthTileSize = DepthInnerSize + 2;

	static constexpr uint32_t MaxProbesPerRow = 144;
	static constexpr uint32_t MaxProbeCount = 32 * 16 * 32;	// 图集按上限一次性分配
	static constexpr uint32_t MaxAxisCount = 255;			// 单轴探针数上限

	static glm::uvec2 AtlasSize(uint32_t tileSize)
	{
		const uint32_t rows = (MaxProbeCount + MaxProbesPerRow - 1) / MaxProbesPerRow;
		return glm::uvec2(MaxProbesPerRow * tileSize, rows * tileSize);
	}

	static glm::uvec2 RayDataSize()         { return AtlasSize(RayGridSize); }
	static glm::uvec2 IrradianceAtlasSize() { return AtlasSize(IrradianceTileSize); }
	static glm::uvec2 DepthAtlasSize()      { return AtlasSize(DepthTileSize); }
	static glm::uvec2 ProbeStateSize()      { return AtlasSize(1); }
};

// DDGIBlend.comp 一个 workgroup（RaysPerProbe 个线程）处理一个探针：每个线程最多负责一个内层 texel，
// 边框阶段每个线程最多回填一个边框 texel
static_assert(DDGIAtlasLayout::IrradianceInnerSize * DDGIAtlasLayout::IrradianceInnerSize <= DDGIAtlasLayout::RaysPerProbe);
static_assert(DDGIAtlasLayout::DepthInnerSize * DDGIAtlasLayout::DepthInnerSize <= DDGIAtlasLayout::RaysPerProbe);
static_assert((4 * DDGIAtlasLayout::IrradianceTileSize - 4) + (4 * DDGIAtlasLayout::DepthTileSize - 4) <= DDGIAtlasLayout::RaysPerProbe);

// 布局常量以宏的形式注入 DDGI shader，shader 侧不再各自硬编码。
// 所有 DDGI Pass 创建管线时都必须调用（shader 里缺宏会直接 #error）。
template <typename PipelineConfigT>
inline void DDGIAddLayoutDefines(PipelineConfigT& config)
{
	config.AddDefineMacro("DDGI_RAY_GRID", DDGIAtlasLayout::RayGridSize);
	config.AddDefineMacro("DDGI_IRR_INNER", DDGIAtlasLayout::IrradianceInnerSize);
	config.AddDefineMacro("DDGI_DEPTH_INNER", DDGIAtlasLayout::DepthInnerSize);
	config.AddDefineMacro("DDGI_PROBES_PER_ROW", DDGIAtlasLayout::MaxProbesPerRow);
}

// ───────────────────────────── shader UBO ─────────────────────────────

// std140，GLSL 镜像在 shader/DDGI/DDGIUniforms.comp
struct alignas(16) DDGIUniforms
{
	glm::vec4  volumeOrigin;	// xyz 体积最小角（世界空间，米）
	glm::vec4  volumeSpacing;	// xyz 探针间距，w maxRayDistance
	glm::uvec4 volumeCounts;	// xyz 每轴探针数，w 总数
	glm::vec4  shadingParams;	// x normalBias，y viewBias，z energy，w skyIntensity
	glm::vec4  filterParams;	// x 背面权重指数，y 时域混合系数（重置帧为 1），z 深度截断距离，w 光照突变阈值
	glm::uvec4 runParams;		// x sliceCount，y frameIndex，z history 可读（多次反弹），w 天空 cubemap 可用
	glm::uvec4 scrollOffset;	// xyz 槽位偏移，w 本帧发生整格平移
	glm::ivec4 scrollDelta;		// xyz 本帧平移格数，w 本帧全量重置
	glm::vec4  cameraPos;		// xyz 相机位置
};

static_assert(sizeof(DDGIUniforms) == 144, "DDGIUniforms 与 GLSL 侧 std140 布局不一致");

// ───────────────────────────── 探针体积 ─────────────────────────────

// 轴对齐的规则探针网格（v1 只支持单体积）
struct DDGIVolume
{
	glm::vec3 origin = glm::vec3(-32.0f);
	glm::vec3 spacing = glm::vec3(2.0f);
	glm::uvec3 counts = glm::uvec3(16, 8, 16);
	uint32_t totalProbes = 16u * 8u * 16u;
};

inline glm::vec3 DDGIVolumeExtent(const DDGIVolume& volume)
{
	return glm::vec3(volume.counts) * volume.spacing;
}

// 由包围盒拟合体积：
//   * 每边外扩 max(padding, spacing)：采样端的边缘淡出占一整格，外扩不足一格时贴着包围盒的表面（典型是地面）会被削暗；
//   * origin 向下 snap 到 spacing 整数倍，包围盒轻微变化时探针不会滑动；
//   * 超限时放大 spacing 而不是截断 counts，体积始终覆盖整个场景：
//     先只放大超过单轴上限的轴，单轴都合法后若总数仍超限，再按 cbrt 比例整体放大。
inline DDGIVolume DDGIFitVolumeFromBounds(
	const glm::vec3& sceneMin,
	const glm::vec3& sceneMax,
	const glm::vec3& spacing,
	float padding)
{
	constexpr uint32_t MaxAxis = DDGIAtlasLayout::MaxAxisCount;
	constexpr uint32_t MaxProbes = DDGIAtlasLayout::MaxProbeCount;

	glm::vec3 sp = glm::max(spacing, glm::vec3(0.25f));
	const glm::vec3 basePad(std::max(0.0f, padding));

	glm::vec3 origin(0.0f);
	glm::uvec3 counts(1u);

	auto layout = [&](const glm::vec3& s)
		{
			const glm::vec3 pad = glm::max(basePad, s);
			origin = glm::floor((sceneMin - pad) / s) * s;
			const glm::vec3 countF = glm::max(glm::ceil((sceneMax + pad - origin) / s), glm::vec3(1.0f));
			counts = glm::uvec3(glm::min(countF, glm::vec3(1.0e6f)));	// 防极端尺寸溢出
		};

	DDGIVolume volume;
	for (uint32_t attempt = 0; attempt < 64; ++attempt)
	{
		layout(sp);

		const uint64_t total = uint64_t(counts.x) * uint64_t(counts.y) * uint64_t(counts.z);
		const bool axisOk = counts.x <= MaxAxis && counts.y <= MaxAxis && counts.z <= MaxAxis;
		if (axisOk && total <= MaxProbes)
		{
			volume.origin = origin;
			volume.spacing = sp;
			volume.counts = counts;
			volume.totalProbes = uint32_t(total);
			return volume;
		}

		if (!axisOk)
		{
			for (int a = 0; a < 3; ++a)
			{
				if (counts[a] > MaxAxis)
					sp[a] *= float(counts[a]) / float(MaxAxis) * 1.02f;
			}
			continue;
		}

		sp *= float(std::cbrt(double(total) / double(MaxProbes))) * 1.02f;
	}

	// 兜底（正常走不到）：按立方根硬夹，保证不越界
	layout(sp);
	const uint32_t cap = std::min(MaxAxis, uint32_t(std::cbrt(double(MaxProbes))));
	counts = glm::min(counts, glm::uvec3(cap));
	volume.origin = origin;
	volume.spacing = sp;
	volume.counts = counts;
	volume.totalProbes = counts.x * counts.y * counts.z;
	return volume;
}

// DDGIParams -> 本帧使用的体积。优先级：autoFitVolume > followCamera > 手动 center。
inline DDGIVolume DDGIResolveVolume(
	const Scene3DRenderOption::DDGIParams& params,
	const glm::vec3& sceneMin,
	const glm::vec3& sceneMax,
	bool sceneBoundsValid,
	const glm::vec3& cameraPos)
{
	DDGIVolume volume;
	volume.spacing = glm::max(params.spacing, glm::vec3(0.25f));

	// 场景里还没有静态网格时退回手动参数，避免开局体积为 1×1×1
	if (params.autoFitVolume && sceneBoundsValid)
		return DDGIFitVolumeFromBounds(sceneMin, sceneMax, volume.spacing, params.fitPadding);

	// 手动 / 跟随相机：保持用户给的 spacing，总数超限时按比例缩小 counts
	glm::uvec3 counts = glm::clamp(params.counts, glm::uvec3(1u), glm::uvec3(DDGIAtlasLayout::MaxAxisCount));
	const uint64_t total = uint64_t(counts.x) * uint64_t(counts.y) * uint64_t(counts.z);
	if (total > DDGIAtlasLayout::MaxProbeCount)
	{
		const float s = std::cbrt(float(DDGIAtlasLayout::MaxProbeCount) / float(total));
		counts = glm::max(glm::uvec3(glm::vec3(counts) * s), glm::uvec3(1u));
	}

	volume.counts = counts;
	volume.totalProbes = counts.x * counts.y * counts.z;

	// origin snap 到 spacing 整数倍：跟随相机时体积只按整格跳，跳动由滚动更新处理
	const glm::vec3 center = params.followCamera ? cameraPos : params.center;
	const glm::vec3 halfExtent = glm::vec3(counts) * volume.spacing * 0.5f;
	volume.origin = glm::floor((center - halfExtent) / volume.spacing) * volume.spacing;

	return volume;
}

// ───────────────────────────── 跨帧共享状态 ─────────────────────────────

// 本帧解析结果。三个 DDGI Pass 必须拿到逐位相同的一份（体积、轮转、滚动偏移任一不一致都会取错探针）。
struct DDGIFrameState
{
	DDGIVolume volume{};
	uint32_t   sliceCount = 1;			// 本帧轮转份数，全量重置帧为 1
	uint32_t   frameIndex = 0;			// 已取模；决定轮转相位与光线随机旋转
	bool       fullReset = true;		// 首帧 / 帧不连续 / 形状变化 / 平移超出窗口
	bool       shapeChanged = true;		// spacing 或 counts 变化（用于日志）
	bool       scrolled = false;		// 形状不变、整格平移：只重置新进入窗口的探针层
	glm::ivec3 scrollDelta{ 0 };		// 本帧平移格数（新逻辑坐标 = 旧逻辑坐标 - delta）
	glm::uvec3 scrollOffset{ 0u };
	bool       historyValid = false;	// 上一帧图集与本帧映射一致，可用于多次反弹
	float      blendAlpha = 1.0f;		// 1 - hysteresis；全量重置时为 1（直接覆盖）
};

// 按 frameIndex 缓存解析结果：同一帧谁先调用谁计算，其余 Pass 直接取缓存。
// FrameBegin 由 Graph 并行调度，内部加锁。
class DDGISharedState
{
public:
	DDGIFrameState Resolve(
		uint32_t frameIndex,
		const Scene3DRenderOption::DDGIParams& params,
		const glm::vec3& sceneMin,
		const glm::vec3& sceneMax,
		bool sceneBoundsValid,
		const glm::vec3& cameraPos)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		for (const auto& e : _recent)
		{
			if (e.valid && e.frame == frameIndex)
				return e.state;
		}

		DDGIFrameState s;
		s.volume = DDGIResolveVolume(params, sceneMin, sceneMax, sceneBoundsValid, cameraPos);
		s.frameIndex = frameIndex % 100000u;

		// 乱序到达的旧帧（正常不会发生）：给一份全量重置结果，不推进状态
		if (_hasLast && frameIndex < _lastFrame)
			return s;

		// 帧号不连续说明 DDGI 中途关闭过，history 不可信
		const bool contiguous = _hasLast && frameIndex == _lastFrame + 1;
		const bool sameShape = contiguous
			&& s.volume.spacing == _lastVolume.spacing
			&& s.volume.counts == _lastVolume.counts;

		s.shapeChanged = !sameShape;
		s.fullReset = !sameShape;

		if (sameShape && s.volume.origin != _lastVolume.origin)
		{
			const glm::ivec3 delta = glm::ivec3(glm::round((s.volume.origin - _lastVolume.origin) / s.volume.spacing));
			const glm::ivec3 c = glm::ivec3(s.volume.counts);
			if (std::abs(delta.x) >= c.x || std::abs(delta.y) >= c.y || std::abs(delta.z) >= c.z)
			{
				s.fullReset = true;
			}
			else
			{
				s.scrolled = true;
				s.scrollDelta = delta;
				for (int a = 0; a < 3; ++a)
					_offset[a] = uint32_t((((int(_offset[a]) + delta[a]) % c[a]) + c[a]) % c[a]);
			}
		}

		if (s.fullReset)
			_offset = glm::uvec3(0u);

		s.scrollOffset = _offset;
		s.sliceCount = s.fullReset ? 1u : std::max(1u, params.updateSliceCount);
		s.historyValid = !s.fullReset;
		s.blendAlpha = s.fullReset ? 1.0f : 1.0f - glm::clamp(params.hysteresis, 0.0f, 0.98f);

		_hasLast = true;
		_lastFrame = frameIndex;
		_lastVolume = s.volume;

		_recent[_next] = Entry{ true, frameIndex, s };
		_next = (_next + 1) % uint32_t(_recent.size());
		return s;
	}

private:
	struct Entry
	{
		bool valid = false;
		uint32_t frame = 0;
		DDGIFrameState state{};
	};

	std::mutex _mutex;
	std::array<Entry, 4> _recent{};
	uint32_t _next = 0;

	bool _hasLast = false;
	uint32_t _lastFrame = 0;
	DDGIVolume _lastVolume{};
	glm::uvec3 _offset{ 0u };
};

// 由本帧状态和参数拼 UBO。三个 Pass 用同一份输入调用，结果一致。
inline DDGIUniforms DDGIBuildUniforms(
	const DDGIFrameState& s,
	const Scene3DRenderOption::DDGIParams& p,
	uint32_t cubeEnable,
	const glm::vec3& cameraPos)
{
	const DDGIVolume& volume = s.volume;
	const float maxRayDistance = std::max(0.1f, p.maxRayDistance);

	// 深度只关心一格之内的遮挡：截断到 1.5 倍格子对角线，提高 16F 精度，
	// 也避免未命中光线（t = maxRayDistance）把均值和方差拉飞
	const float depthClamp = std::min(maxRayDistance, glm::length(volume.spacing) * 1.5f);

	DDGIUniforms u{};
	u.volumeOrigin = glm::vec4(volume.origin, 1.0f);
	u.volumeSpacing = glm::vec4(volume.spacing, maxRayDistance);
	u.volumeCounts = glm::uvec4(volume.counts, volume.totalProbes);
	u.shadingParams = glm::vec4(
		std::max(0.0f, p.normalBias),
		std::max(0.0f, p.viewBias),
		std::max(0.0f, p.energy),
		std::max(0.0f, p.skyIntensity));
	u.filterParams = glm::vec4(std::max(0.001f, p.backfaceThreshold), s.blendAlpha, depthClamp, std::max(0.0f, p.lightChangeThreshold));
	u.runParams = glm::uvec4(std::max(1u, s.sliceCount), s.frameIndex, s.historyValid ? 1u : 0u, cubeEnable);
	u.scrollOffset = glm::uvec4(s.scrollOffset, s.scrolled ? 1u : 0u);
	u.scrollDelta = glm::ivec4(s.scrollDelta, s.fullReset ? 1 : 0);
	u.cameraPos = glm::vec4(cameraPos, 1.0f);
	return u;
}
