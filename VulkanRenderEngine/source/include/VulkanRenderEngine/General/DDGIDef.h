#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include "VulkanRenderEngine/RenderOption.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/General/DDGIVolumeDesc.h"
#include "VulkanRenderEngine/General/DDGIDebug.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"

struct RenderState;

// DDGI 公共定义：图集布局、GPU 数据结构、跨帧共享状态。
//
// 多体积共用一个「探针池」：所有资源都按全局槽位寻址，每帧 CPU 把启用的体积依次排进池里，
// 每个体积占一段连续槽位 [base, base + count)。槽位每帧重新分配、不承载身份；
// 探针的身份是 (体积 ID, 世界格子坐标)，历史数据按上一帧的布局找回（见 DDGIVolumeGPU 的 prev 字段）。
//
// 资源（按槽位寻址，tile 规则 = (slot % probesPerRow, slot / probesPerRow) × tileSize）：
//   rayData     本帧更新列表里第 i 个探针的 RayGrid² 条光线；rgb = 辐亮度，a = 命中距离（负 = 背面）
//   irradiance  IrradianceInner² + 1px 边框；rgb = 余弦卷积后的 E/π
//   depth       DepthInner² + 1px 边框；rg = (E[t], E[t²])
//   probeData   1 texel；xyz = 探针偏移（米），w = 已更新次数（0 = 无效）
//   probeStats  1 texel；x = 背面命中比例，yzw = 光照突变检测统计量

struct DDGIAtlasLayout
{
	static constexpr uint32_t RayGridSize = 16;				// 每探针 16×16 = 256 条光线
	static constexpr uint32_t RaysPerProbe = RayGridSize * RayGridSize;
	static constexpr uint32_t IrradianceInnerSize = 8;
	static constexpr uint32_t IrradianceTileSize = IrradianceInnerSize + 2;
	static constexpr uint32_t DepthInnerSize = 16;
	static constexpr uint32_t DepthTileSize = DepthInnerSize + 2;

	static constexpr uint32_t MaxVolumes = 32;
	static constexpr uint32_t MaxAxisCount = 255;
	static constexpr uint32_t MaxCascadeLevels = 4;
	static constexpr uint32_t LeakMaxVolumes = 4;
	static constexpr uint32_t LeakMaxProbes = LeakMaxVolumes * 8;

	// 体积标志（DDGIVolumeGPU::prevCounts.w）
	static constexpr uint32_t VolumeFlagCascade = 1u;
	static constexpr uint32_t VolumeFlagAutoRelocate = 2u;
	static constexpr uint32_t VolumeFlagHistory = 4u;
	// 探针标志（probeInfo.w）
	static constexpr uint32_t ProbeFlagDisabled = 1u;
	static constexpr uint32_t ProbeFlagLocked = 2u;
	static constexpr uint32_t ProbeFlagInactive = 4u;		// 已弃用：「被更高优先级体积完全覆盖」改为按体积表现算（shader 的 ddgiProbeCovered / CPU 的覆盖范围），CPU 不再写这一位
	static constexpr uint32_t ProbeFlagSelected = 8u;

	// 运行时布局：启动时按设备的纹理尺寸上限确定
	struct Config
	{
		uint32_t capacity = 65536;			// 探针总容量
		uint32_t budget = 8192;				// 每帧更新上限（rayData 容量，也是光追 launch.z，不超过 65535）
		uint32_t probesPerRow = 256;
		uint32_t rows = 256;
		uint32_t rayRows = 32;
		bool irradianceR11 = true;			// 辐照度用 R11G11B10，否则退回 RGBA16F
	};

	static Config& Get()
	{
		static Config config;
		return config;
	}

	// 每行探针数取 min(纹理上限 / 18, ceil(sqrt(容量)))，让图集接近正方形；放不下时截断容量
	static void Configure(uint32_t maxImageDimension, bool r11StorageSupported)
	{
		Config& c = Get();
		const uint32_t maxDim = std::max(maxImageDimension, DepthTileSize);
		const uint32_t requested = std::max(1u, GlobalConfig::DDGIProbeCapacity);
		const uint32_t maxPerRow = std::max(1u, maxDim / DepthTileSize);

		c.probesPerRow = std::clamp(uint32_t(std::ceil(std::sqrt(double(requested)))), 1u, maxPerRow);
		c.rows = (requested + c.probesPerRow - 1) / c.probesPerRow;
		const uint32_t maxRows = std::max(1u, maxDim / DepthTileSize);
		if (c.rows > maxRows)
		{
			c.rows = maxRows;
			std::printf("[ DDGI ] probe capacity %u exceeds the atlas limit (max texture %u), clamped to %u\n",
				requested, maxImageDimension, c.rows * c.probesPerRow);
		}
		c.capacity = std::min(requested, c.rows * c.probesPerRow);
		c.budget = std::clamp(GlobalConfig::DDGIUpdateBudget, 1u, std::min(c.capacity, 65535u));
		c.rayRows = (c.budget + c.probesPerRow - 1) / c.probesPerRow;
		c.irradianceR11 = r11StorageSupported;
	}

	static glm::uvec2 TileAtlasSize(uint32_t tileSize) { const Config& c = Get(); return { c.probesPerRow * tileSize, c.rows * tileSize }; }
	static glm::uvec2 RayDataSize() { const Config& c = Get(); return { c.probesPerRow * RayGridSize, c.rayRows * RayGridSize }; }
	static glm::uvec2 IrradianceAtlasSize() { return TileAtlasSize(IrradianceTileSize); }
	static glm::uvec2 DepthAtlasSize() { return TileAtlasSize(DepthTileSize); }
	static glm::uvec2 ProbeTexelSize() { return TileAtlasSize(1); }
	static vk::Format IrradianceFormat() { return Get().irradianceR11 ? vk::Format::eB10G11R11UfloatPack32 : vk::Format::eR16G16B16A16Sfloat; }
};

// DDGIBlend.comp 一个 workgroup（RaysPerProbe 个线程）处理一个探针：每个线程最多负责一个内层 texel，
// 边框阶段每个线程最多回填一个边框 texel
static_assert(DDGIAtlasLayout::IrradianceInnerSize * DDGIAtlasLayout::IrradianceInnerSize <= DDGIAtlasLayout::RaysPerProbe);
static_assert(DDGIAtlasLayout::DepthInnerSize * DDGIAtlasLayout::DepthInnerSize <= DDGIAtlasLayout::RaysPerProbe);
static_assert((4 * DDGIAtlasLayout::IrradianceTileSize - 4) + (4 * DDGIAtlasLayout::DepthTileSize - 4) <= DDGIAtlasLayout::RaysPerProbe);

// 布局常量以宏的形式注入 DDGI shader，shader 侧不再各自硬编码（缺宏会直接 #error）
template <typename PipelineConfigT>
inline void DDGIAddLayoutDefines(PipelineConfigT& config)
{
	config.AddDefineMacro("DDGI_RAY_GRID", DDGIAtlasLayout::RayGridSize);
	config.AddDefineMacro("DDGI_IRR_INNER", DDGIAtlasLayout::IrradianceInnerSize);
	config.AddDefineMacro("DDGI_DEPTH_INNER", DDGIAtlasLayout::DepthInnerSize);
	config.AddDefineMacro("DDGI_PROBES_PER_ROW", DDGIAtlasLayout::Get().probesPerRow);
	config.AddDefineMacro("DDGI_MAX_LEAK_PROBES", DDGIAtlasLayout::LeakMaxProbes);
	config.AddDefineMacro("DDGI_IRR_FORMAT", std::string(DDGIAtlasLayout::Get().irradianceR11 ? "r11f_g11f_b10f" : "rgba16f"));
}

// ───────────────────────────── GPU 数据 ─────────────────────────────

// std140，GLSL 镜像在 shader/DDGI/DDGIUniforms.comp
struct alignas(16) DDGIUniforms
{
	glm::vec4  shadingParams;	// x normalBias，y viewBias，z energy，w skyIntensity
	glm::vec4  filterParams;	// x 背面权重指数，y 时域混合系数 1-hysteresis，z 光照突变阈值，w maxRayDistance
	glm::vec4  drawParams;		// x 探针球半径（相对最小间距）
	glm::uvec4 counts;			// x 体积数，y 已用槽位数，z 本帧更新数，w frameIndex
	glm::uvec4 flags;			// x 历史可用（帧号连续），y 天空 cubemap 可用（Pass 各自填），z 烘焙体积下标，w 漏光调试开关
	glm::uvec4 debug;			// xy 漏光调试像素，z 可视化模式，w 只显示的体积下标（0xFFFFFFFF = 全部）
	glm::vec4  cameraPos;		// xyz 相机位置
};
static_assert(sizeof(DDGIUniforms) == 112, "DDGIUniforms 与 GLSL 侧 std140 布局不一致");

// std430，GLSL 镜像在 shader/DDGI/DDGIUniforms.comp
struct alignas(16) DDGIVolumeGPU
{
	glm::vec4  origin;			// xyz 最小角（世界），w 边缘过渡宽度（格）
	glm::vec4  spacing;			// xyz 间距，w 优先级（只用于调试显示）
	glm::uvec4 counts;			// xyz 每轴探针数，w base（本帧起始槽位）
	glm::ivec4 prevShift;		// xyz = 本帧最小角格子 - 上一帧最小角格子，w 上一帧 base
	glm::uvec4 prevCounts;		// xyz 上一帧每轴探针数，w 标志（VolumeFlag*）
	glm::uvec4 info;			// x ID，y 轮转周期，z 本帧相位
	glm::vec4  prevOrigin;		// xyz 上一帧最小角（世界）
};
static_assert(sizeof(DDGIVolumeGPU) == 112, "DDGIVolumeGPU 与 GLSL 侧 std430 布局不一致");

// 漏光调试记录（std430，GLSL 镜像在 DDGIApply.comp）
struct alignas(16) DDGILeakProbeGPU
{
	glm::vec4 position;			// xyz 探针位置，w 记录里的体积下标
	glm::vec4 weights;			// x 三线性，y 背面，z 可见性，w 最终
	glm::vec4 extra;			// x 平均命中距离，y 距离，z 1 - 背面剔除，w 是否有效
	glm::ivec4 cell;			// xyz 世界格子，w 槽位
};

struct alignas(16) DDGILeakRecordGPU
{
	glm::uvec4 header;			// x 有效，y 探针记录数，z 体积记录数，w frameIndex
	glm::vec4 worldPos;
	glm::vec4 normal;
	glm::vec4 biasedPos;
	glm::vec4 result;
	glm::vec4 volumeWeights;
	glm::uvec4 volumeIds;
	DDGILeakProbeGPU probes[DDGIAtlasLayout::LeakMaxProbes];
};

// 主机可见的 buffer：WriteData 直接 memcpy，不走 staging + SubmitNowAndWait（那条路会让 CPU 等 GPU 排空队列）。
// DDGI 每帧上传的小数据都用它；每帧新建一份，旧的由 shared_ptr 引用计数 + Retire 延迟释放，不会和在途帧冲突
template <typename Base>
class DDGIHostBlock : public Base
{
public:
	explicit DDGIHostBlock(uint64_t size) : Base(16)
	{
		size = std::max<uint64_t>(size, 16);
		this->_curBuffer = std::make_shared<VKWrapper::VmaBuffer>(this->_device, size, this->_usage, true);
		this->_size = size;
	}
};

template <typename Base>
inline std::shared_ptr<Base> DDGIMakeHostBlock(const void* data, size_t size)
{
	auto block = std::make_shared<DDGIHostBlock<Base>>(uint64_t(size));
	if (data && size > 0)
		block->WriteData(data, size);
	return block;
}

// ───────────────────────────── 每帧解析结果 ─────────────────────────────

struct DDGIResolvedVolume
{
	uint32_t id = 0;
	bool cascade = false;
	uint32_t level = 0;
	glm::ivec3 originCell = glm::ivec3(0);
	glm::vec3 spacing = glm::vec3(1.0f);
	glm::uvec3 counts = glm::uvec3(1u);
	uint32_t probeCount = 1;
	float priority = 0.0f;
	float fadeCells = 1.0f;
	bool autoRelocate = true;
	std::shared_ptr<const DDGIProbeEditMap> edits;

	uint32_t base = 0;
	bool hasHistory = false;
	glm::ivec3 prevOriginCell = glm::ivec3(0);
	glm::uvec3 prevCounts = glm::uvec3(0u);
	uint32_t prevBase = 0;

	float importance = 1.0f;
	uint32_t period = 1;
	uint32_t phase = 0;

	glm::vec3 Origin() const { return glm::vec3(originCell) * spacing; }
	glm::vec3 Extent() const { return glm::vec3(counts) * spacing; }
};

// 三个 DDGI Pass 与可视化 Pass 共享同一份：同一帧逐位一致
struct DDGIFrame
{
	uint32_t frameIndex = 0;
	bool enabled = false;				// 有可用体积
	bool historyValid = false;
	std::vector<DDGIResolvedVolume> volumes;
	uint32_t totalProbes = 0;
	uint32_t updateCount = 0;
	DDGIUniforms uniforms{};			// 需要改字段的 Pass（探针更新）自己再上传一份
	std::shared_ptr<UniformBlock> uniformBlock;		// uniforms 的上传结果，其余 Pass 直接绑定

	std::shared_ptr<StorageBlock> volumeTable;
	std::shared_ptr<StorageBlock> updateList;
	std::shared_ptr<StorageBlock> slotToRay;		// 每槽位：本帧在 updateList 里的下标，没轮到 = 0xFFFFFFFF
	std::shared_ptr<StorageBlock> probeInfo;

	bool bakeActive = false;
	uint32_t bakeVolumeIndex = 0xFFFFFFFFu;
	uint64_t bakeRequestId = 0;

	DDGIDebugSettings debug;
};

// 按 frameIndex 缓存解析结果：同一帧谁先调用谁计算，其余 Pass 直接取缓存（FrameBegin 并行调度，内部加锁）
class DDGISharedState
{
public:
	explicit DDGISharedState(std::shared_ptr<DDGIDebugChannel> channel);

	std::shared_ptr<const DDGIFrame> Resolve(const RenderState& state);

	const std::shared_ptr<DDGIDebugChannel>& GetDebugChannel() const { return _channel; }

private:
	struct LastVolume
	{
		glm::ivec3 originCell;
		glm::uvec3 counts;
		glm::vec3 spacing;
		uint32_t base;
	};

	// probeInfo 只装手动编辑（禁用 / 锁定 / 偏移）与选中，只在这些或手动体积的布局变化时重建；
	// 级联没有编辑，平移时不触发重建
	struct ProbeCache
	{
		bool valid = false;
		std::vector<uint64_t> signature;
		std::shared_ptr<StorageBlock> probeInfo;
		std::vector<uint8_t> disabled;		// 每槽位：被手动禁用
	};

	// 体积 i 被更高优先级体积 j 完全覆盖的部分，换算成体积 i 的局部格子下标范围（闭区间）。
	// 判定与 shader 的 ddgiProbeCovered 用同一个公式，每帧按体积表重算（只和体积数有关，与探针数无关）
	struct CoveredRange
	{
		glm::ivec3 lo;
		glm::ivec3 hi;
	};

	void BuildVolumes(const RenderState& state, DDGIFrame& frame, std::vector<DDGIResolvedVolume>& dropped);
	void AssignPeriods(const RenderState& state, DDGIFrame& frame, uint32_t budget);
	void RebuildProbeCache(const DDGIFrame& frame);
	void BuildCoveredRanges(const DDGIFrame& frame);
	bool IsCovered(size_t volumeIndex, const glm::ivec3& local) const;
	void BuildUpdateList(DDGIFrame& frame, uint32_t budget);
	void UploadGPUData(DDGIFrame& frame, const RenderState& state);
	void PublishStats(const DDGIFrame& frame, uint32_t budget, const std::vector<DDGIResolvedVolume>& dropped);

	std::shared_ptr<DDGIDebugChannel> _channel;
	std::mutex _mutex;

	std::array<std::shared_ptr<const DDGIFrame>, 4> _recent{};
	uint32_t _next = 0;

	bool _hasLast = false;
	uint32_t _lastFrame = 0;
	std::unordered_map<uint32_t, LastVolume> _lastVolumes;

	ProbeCache _cache;
	std::vector<std::vector<CoveredRange>> _covered;	// 每体积一组，见 CoveredRange
	std::vector<uint32_t> _scratchList;
	std::vector<uint32_t> _scratchMask;
};

// 世界格子哈希（CPU 用来决定探针的轮转相位）
inline uint32_t DDGIHashCell(uint32_t volumeId, const glm::ivec3& c)
{
	uint32_t h = volumeId * 0x9E3779B1u;
	h ^= uint32_t(c.x) * 0x85EBCA77u;
	h ^= uint32_t(c.y) * 0xC2B2AE3Du;
	h ^= uint32_t(c.z) * 0x27D4EB2Fu;
	h ^= h >> 15; h *= 0x2C1B3C6Du;
	h ^= h >> 12; h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

// 级联体积的 ID 段（实体 ID 不会用到）
inline constexpr uint32_t DDGICascadeVolumeId(uint32_t level) { return 0xFFFF0000u + level; }
