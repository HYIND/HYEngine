#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/EditorOutlinePass.h"
#include "VulkanRenderEngine/General/EditorViewport.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

namespace
{
	// 和 selectionOutline.comp 的 push_constant 一致
	struct alignas(16) OutlineParams
	{
		glm::vec4 primaryColor;
		glm::vec4 secondaryColor;
		glm::vec4 params;		// x 宽度（像素），y 被挡住部分的透明度，z 被挡住部分的内部叠色强度
	};
	static_assert(sizeof(OutlineParams) == 48);
}

EditorOutlinePass::EditorOutlinePass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = computeShaderPath;
	config
		.AddStorageImage(0)			// 最终画面（原地修改）
		.AddUnifromTexture(1)		// 选中遮罩
		.AddPushConstant(sizeof(OutlineParams));

	if (config.Validate())
		_shader.Create(config);
}

void EditorOutlinePass::Draw(const std::shared_ptr<Texture2D>& finalColor, const std::shared_ptr<Texture2D>& selectionMask, const EditorFrameSelection& frame)
{
	if (!_shader.IsValid() || !frame.maskDrawn.load(std::memory_order_acquire))
		return;
	if (!finalColor || finalColor->IsEmpty() || !selectionMask || selectionMask->IsEmpty())
		return;
	const uint32_t width = finalColor->GetWidth();
	const uint32_t height = finalColor->GetHeight();
	if (selectionMask->GetWidth() != width || selectionMask->GetHeight() != height)
		return;

	const EditorSelectionStyle& style = frame.style;
	OutlineParams params{
		.primaryColor = style.primaryColor,
		.secondaryColor = style.secondaryColor,
		.params = glm::vec4(style.widthPx, style.occludedAlpha, style.occludedFill, 0.0f)
	};

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return;

	finalColor->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	selectionMask->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	ComputeBindingRecord binding;
	binding.SetStorageImage(finalColor, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(selectionMask, vk::ImageAspectFlagBits::eColor, 1);

	_shader.SetPushConstants(cmd, &params, sizeof(params));
	_shader.Bind(cmd, binding);
	cmd->dispatch(
		(width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1);

	cmd->SubmitNowAndWait();
}
