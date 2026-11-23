#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

// 场景里手动放置的 DDGI 体积（由 DDGIVolumeComponent 每帧收集），以及单探针的手动修改。
// 这个头文件不依赖 Vulkan，运行时、序列化、编辑器都会包含它。

// 单个探针的手动修改，按「世界格子坐标」索引：体积整格移动、改尺寸时修改跟着世界位置走，
// 只有改 spacing 时才对不上。
struct DDGIProbeEdit
{
	glm::vec3 offset = glm::vec3(0.0f);	// 相对格子中心的偏移（米），渲染时钳到 ±0.45 spacing
	bool disabled = false;				// 禁用：不更新、不参与采样
	bool locked = false;				// 锁定：使用手动 offset，不参与自动重定位
};

using DDGIProbeEditMap = std::unordered_map<int64_t, DDGIProbeEdit>;

// 世界格子坐标 <-> 64 位键（每轴 21 位有符号，范围约 ±104 万格）
inline int64_t DDGIPackCell(const glm::ivec3& c)
{
	constexpr int64_t mask = (int64_t(1) << 21) - 1;
	return ((int64_t(c.x) & mask) << 42) | ((int64_t(c.y) & mask) << 21) | (int64_t(c.z) & mask);
}

inline glm::ivec3 DDGIUnpackCell(int64_t key)
{
	auto field = [](int64_t v) -> int {
		v &= (int64_t(1) << 21) - 1;
		if (v & (int64_t(1) << 20))
			v -= int64_t(1) << 21;
		return int(v);
		};
	return glm::ivec3(field(key >> 42), field(key >> 21), field(key));
}

struct DDGIVolumeDesc
{
	uint32_t id = 0;						// 稳定 ID（实体 ID），用于跨帧对应历史数据
	glm::vec3 center = glm::vec3(0.0f);
	glm::vec3 spacing = glm::vec3(1.0f);
	glm::uvec3 counts = glm::uvec3(8, 4, 8);
	int priority = 10;						// 越大越优先；级联固定低于所有手动体积
	float fadeCells = 1.0f;					// 边缘过渡宽度（格）
	bool autoRelocate = true;
	std::shared_ptr<const DDGIProbeEditMap> edits;	// 写时复制：修改时整体替换指针，不原地改
};

// 体积最小角所在的世界格子：盒子吸附到自身 spacing 的世界网格上
inline glm::ivec3 DDGIVolumeOriginCell(const glm::vec3& center, const glm::vec3& spacing, const glm::uvec3& counts)
{
	const glm::vec3 sp = glm::max(spacing, glm::vec3(0.25f));
	return glm::ivec3(glm::floor((center - glm::vec3(counts) * sp * 0.5f) / sp));
}
