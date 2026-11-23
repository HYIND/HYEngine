#pragma once

#include <memory>
#include "glm/glm.hpp"
#include "ECSCore/IComponent.h"
#include "Components/Renderable.h"
#include "VulkanRenderEngine/General/DDGIVolumeDesc.h"

// 手动摆放的 DDGI 探针体积。中心取 Transform.position（不受旋转 / 缩放影响），
// 盒子尺寸 = counts × spacing，最小角吸附到 spacing 的世界网格上。
// visible = 启用。优先级越大越先采样，所有手动体积都高于跟随相机的级联。
// 单探针修改（偏移 / 禁用 / 锁定）按世界格子坐标存在 edits 里：体积整格移动、改尺寸时修改跟着世界位置走，
// 改 spacing 会让它们对不上（编辑器会提示）。edits 写时复制：修改时整体替换指针。
struct DDGIVolumeComponent : public Renderable
{
	glm::vec3 spacing = glm::vec3(1.0f);
	glm::uvec3 counts = glm::uvec3(8, 4, 8);
	int priority = 10;
	float fadeCells = 1.0f;
	bool autoRelocate = true;
	std::shared_ptr<const DDGIProbeEditMap> edits;

	glm::vec3 Extent() const { return glm::vec3(counts) * spacing; }
};

HY_DECLARE_ECS_TYPE(DDGIVolumeComponent, "hy.engine.component.DDGIVolume");

// 实体 -> 体积 ID（0 保留为「无」，级联用 0xFFFF0000 以上）
inline uint32_t DDGIVolumeIdFromEntity(uint32_t entityId) { return (entityId & 0x7FFFFFFFu) + 1u; }
