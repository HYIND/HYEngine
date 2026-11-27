#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"

// 编辑器选中遮罩：把选中的网格（静态 + 蒙皮，不透明和透明）再画一遍到遮罩图上，给后处理之后的描边（EditorOutlinePass）用。
//   - 不做深度测试，画出完整轮廓（被挡住的部分也画）
//   - 每个像素和实体 ID 缓冲比较：缓冲里就是自己 = 看得见，否则 = 被挡住（比深度比较准，没有精度问题）
//   - 遮罩 RGBA8：r = 被选中物体覆盖，g = 看得见，b = 主选中；混合用 max，多个选中物体重叠时取并集
//   - 按材质透明度 discard，规则和写实体 ID 时一致：不透明 < 0.01、透明 < GeometryPass::TransparentPickOpacity 的地方不算覆盖
//   - 透明物体只在 drawTransparent 打开时画（和 GeometryPass 写透明物体 ID 的条件一致）
// 选中集合取 RenderState::editorSelection（帧派发时的快照）；没选中的网格时不执行，快照的 maskDrawn 保持 false，描边也跳过。
// 只在 GlobalConfig::EditorViewportFeatures 打开时创建（见 VKRenderer3D::InitSceneRenderGraph）。
//   Input 0：实体 ID 缓冲（GeometryPass 的编辑器附件）；External 0：选中遮罩（每个在途帧一张，VKRenderer3D 持有）
class EditorSelectionMaskPass :public RenderPassBase
{
public:
	EditorSelectionMaskPass(
		const std::string& fragmentShaderPath = "shader/editor/selectionMask.fs",
		const std::string& staticVertexShaderPath = "shader/gbuffer/geometrypass_StaticMesh.vs",
		const std::string& skinnedVertexShaderPath = "shader/gbuffer/geometrypass_SkinnedMesh.vs");
	~EditorSelectionMaskPass() override = default;

	static vk::Format GetMaskFormat() { return vk::Format::eR8G8B8A8Unorm; }

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	std::shared_ptr<GraphicsPipeline> _staticShader;
	std::shared_ptr<GraphicsPipeline> _skinnedShader;
	// 没有上一帧蒙皮顶点时占位（蒙皮 VS 只在 motion.enabled 时读它）
	std::shared_ptr<VertexBufferBlock> _dummySkinnedVertexBuffer;
};
