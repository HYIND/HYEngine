#pragma once

#include "RenderPassBase.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

// DDGI 探针可视化（编辑器调试）：每个槽位一个实例化小球，画在场景颜色上、参与深度测试。
// 显示模式、球半径、只显示选中体积等来自 DDGIDebugChannel；开关是 PostProcessFlags::ddgiProbeDrawOn。
// 体积线框与漏光连线由编辑器用 ImGui 叠加绘制，不在这里。
class DDGIProbeDrawPass :public RenderPassBase
{
public:
	DDGIProbeDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~DDGIProbeDrawPass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);

	void SetSharedState(std::shared_ptr<DDGISharedState> sharedState);

private:
	GraphicsPipeline _shader;
	std::shared_ptr<DDGISharedState> _sharedState;

	std::shared_ptr<VertexBufferBlock> _vertexBuffer;
	std::shared_ptr<IndexBufferBlock> _indexBuffer;
	uint32_t _indexCount = 0;
};
