#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"

struct EditorFrameSelection;

// 编辑器选中描边：后处理之后、2D 阶段之前，在最终画面（RGBA8，已色调映射）上原地叠加描边（shader/editor/selectionOutline.comp）。
// 读 EditorSelectionMaskPass 画的遮罩；遮罩这一帧没画东西（frame.maskDrawn = false）时什么都不做。
// 和 GlobalPostProcessPass 一样不在渲染图里，由 VKRenderer3D::Execute 的后处理段调用（各帧共用，同一时间只有一帧在用）。
class EditorOutlinePass
{
public:
	explicit EditorOutlinePass(const std::string& computeShaderPath = "shader/editor/selectionOutline.comp");

	void Draw(const std::shared_ptr<Texture2D>& finalColor, const std::shared_ptr<Texture2D>& selectionMask, const EditorFrameSelection& frame);

private:
	ComputePipeline _shader;
};
