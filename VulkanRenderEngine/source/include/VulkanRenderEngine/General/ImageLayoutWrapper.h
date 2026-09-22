#pragma once

#include "vkstdafx.h"

namespace ImageLayout
{
	enum class BindStage { Graphics = 0, Compute, RayTracing, Transfer };	// 使用场景，图像渲染管线、计算管线、光追管线、传输
	enum class BindUsage { Write = 0, Read };								// 用途,写入(附件，imagestore或者作为传输目标)/读取(采样, unifrom sampler、storage image或者作为传输源)

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