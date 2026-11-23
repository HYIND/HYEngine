#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <glm/glm.hpp>

// 编辑器与渲染器之间的 DDGI 调试通道（线程安全）：
//   编辑器 -> 渲染器：可视化开关、选中的体积 / 探针、漏光调试像素、烘焙请求
//   渲染器 -> 编辑器：运行时统计、漏光调试结果、烘焙结果
// 全部是调试数据，不进场景文件。

enum class DDGIProbeDrawMode : uint32_t
{
	Irradiance = 0,		// 球面显示该探针各方向的辐照度
	State = 1,			// 按状态着色（正常 / 无效 / 禁用 / 被覆盖 / 埋在几何里 / 已偏移 / 锁定）
	Distance = 2		// 按各方向的平均命中距离着色
};

struct DDGIDebugSettings
{
	bool drawProbes = false;
	DDGIProbeDrawMode drawMode = DDGIProbeDrawMode::Irradiance;
	float probeRadius = 0.12f;				// 探针球半径（相对该体积的最小间距）
	bool onlySelectedVolume = false;

	uint32_t selectedVolumeId = 0;			// 0 = 没有选中
	std::shared_ptr<const std::vector<glm::ivec3>> selectedCells;	// 选中体积里被选中的探针（世界格子）
	uint64_t selectionVersion = 0;

	bool leakDebug = false;
	bool leakPixelValid = false;
	glm::vec2 leakPixelUV = glm::vec2(0.5f);	// 视口内归一化坐标
};

struct DDGILeakProbe
{
	glm::vec3 position = glm::vec3(0.0f);
	uint32_t volumeIndex = 0;			// 本条记录所属体积在 DDGILeakResult::volumeIds 里的下标
	glm::ivec3 cell = glm::ivec3(0);
	uint32_t slot = 0;
	float wTrilinear = 0.0f;
	float wBackface = 0.0f;				// ((n·d + 1) / 2)^k + 0.2
	float wVisibility = 0.0f;			// Chebyshev（下限 0.05）
	float wFinal = 0.0f;				// 最终权重（含压低、剔除、三线性），未归一化
	float meanDistance = 0.0f;			// 该方向的平均命中距离
	float distance = 0.0f;				// 采样点到探针的距离
	float interior = 1.0f;				// 1 - 背面剔除系数
	bool valid = false;					// 探针是否已更新过
};

struct DDGILeakResult
{
	bool valid = false;
	uint32_t frameIndex = 0;
	glm::vec3 worldPos = glm::vec3(0.0f);
	glm::vec3 normal = glm::vec3(0.0f);
	glm::vec3 biasedPos = glm::vec3(0.0f);
	glm::vec3 irradiance = glm::vec3(0.0f);
	uint32_t volumeCount = 0;
	uint32_t volumeIds[4] = {};
	float volumeWeights[4] = {};		// 每个体积最终拿到的权重（已扣掉更高优先级体积用掉的部分）
	std::vector<DDGILeakProbe> probes;
};

struct DDGIVolumeRuntimeInfo
{
	uint32_t id = 0;
	bool cascade = false;
	uint32_t level = 0;
	int priority = 0;
	glm::vec3 origin = glm::vec3(0.0f);
	glm::vec3 spacing = glm::vec3(1.0f);
	glm::uvec3 counts = glm::uvec3(0);
	uint32_t base = 0;
	uint32_t probeCount = 0;
	uint32_t updatePeriod = 1;
	bool dropped = false;				// 超出探针总容量被停用
};

struct DDGIRuntimeStats
{
	uint32_t frameIndex = 0;
	uint32_t capacity = 0;
	uint32_t budget = 0;
	uint32_t usedProbes = 0;
	uint32_t updatedProbes = 0;
	std::vector<DDGIVolumeRuntimeInfo> volumes;
};

// 自动重定位结果的烘焙：按体积内线性下标存放，xyz = 偏移（米），w = 已更新次数（0 = 无效）
struct DDGIBakeResult
{
	uint64_t requestId = 0;
	uint32_t volumeId = 0;
	glm::ivec3 originCell = glm::ivec3(0);
	glm::uvec3 counts = glm::uvec3(0);
	glm::vec3 spacing = glm::vec3(1.0f);
	std::vector<glm::vec4> probes;
};

class DDGIDebugChannel
{
public:
	void SetSettings(const DDGIDebugSettings& settings)
	{
		std::lock_guard lock(_mutex);
		_settings = settings;
	}

	DDGIDebugSettings GetSettings() const
	{
		std::lock_guard lock(_mutex);
		return _settings;
	}

	void PublishStats(DDGIRuntimeStats stats)
	{
		std::lock_guard lock(_mutex);
		_stats = std::move(stats);
	}

	DDGIRuntimeStats GetStats() const
	{
		std::lock_guard lock(_mutex);
		return _stats;
	}

	void PublishLeak(DDGILeakResult result)
	{
		std::lock_guard lock(_mutex);
		_leak = std::move(result);
	}

	DDGILeakResult GetLeak() const
	{
		std::lock_guard lock(_mutex);
		return _leak;
	}

	// 编辑器发起烘焙请求，返回请求号
	uint64_t RequestBake(uint32_t volumeId)
	{
		std::lock_guard lock(_mutex);
		_bakeRequestVolume = volumeId;
		_bakeRequestId = ++_bakeCounter;
		return _bakeRequestId;
	}

	// 渲染器取走待处理的请求
	bool TakeBakeRequest(uint32_t& volumeId, uint64_t& requestId)
	{
		std::lock_guard lock(_mutex);
		if (_bakeRequestId == 0)
			return false;
		volumeId = _bakeRequestVolume;
		requestId = _bakeRequestId;
		_bakeRequestId = 0;
		return true;
	}

	void PublishBake(DDGIBakeResult result)
	{
		std::lock_guard lock(_mutex);
		_bakeResult = std::move(result);
		_hasBakeResult = true;
	}

	bool TakeBakeResult(DDGIBakeResult& out)
	{
		std::lock_guard lock(_mutex);
		if (!_hasBakeResult)
			return false;
		out = std::move(_bakeResult);
		_hasBakeResult = false;
		return true;
	}

private:
	mutable std::mutex _mutex;
	DDGIDebugSettings _settings;
	DDGIRuntimeStats _stats;
	DDGILeakResult _leak;

	uint64_t _bakeCounter = 0;
	uint64_t _bakeRequestId = 0;
	uint32_t _bakeRequestVolume = 0;
	DDGIBakeResult _bakeResult;
	bool _hasBakeResult = false;
};
