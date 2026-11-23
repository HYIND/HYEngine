#pragma once

#include "vkstdafx.h"

namespace ImageLayout
{
	enum class BindStage { Graphics = 0, Compute, RayTracing, Transfer };	// 使用场景，图像渲染管线、计算管线、光追管线、传输
	// 用途：
	//   Write  写入：附件、imageStore、传输目标
	//   Read   读取：storage image 读（imageLoad）、传输源；Compute / RayTracing 下为 GENERAL
	//   Sample 采样读（sampler2D 等 combined image sampler）。规范只按描述符类型限制布局，不按管线阶段：
	//          采样在任何着色器阶段都可以用 *_READ_ONLY_OPTIMAL；只有 storage image 必须是 GENERAL。
	//          Graphics / Transfer 下与 Read 相同。
	enum class BindUsage { Write = 0, Read, Sample };

	void GetImageLayoutAndStageFlag(vk::ImageLayout* outLayout, vk::PipelineStageFlags* outDestStageFlag, vk::Format format, BindStage stage = BindStage::Graphics, BindUsage usage = BindUsage::Read);


	uint32_t GetFormatSize(vk::Format format);
	bool IsColorFormat(vk::Format format);
	bool IsDepthStencilFormat(vk::Format format);
	bool IsDepthFormat(vk::Format format);
	bool IsStencilFormat(vk::Format format);
	bool IsLinearFormat(vk::Format format);
	vk::AccessFlags GetAccessMaskForLayout(vk::ImageLayout layout, vk::PipelineStageFlags dstStageMask);
	vk::PipelineStageFlags AccessMaskToStage(vk::AccessFlags mask);
	vk::ImageAspectFlags GetAspectMaskForFormat(vk::Format format);
}