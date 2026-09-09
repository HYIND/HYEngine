#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "./RenderPassBase.h"

class LightDrawPass :public RenderPassBase
{
public:
	LightDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~LightDrawPass() = default;
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	GraphicsPipeline _shader;
	std::shared_ptr<StorageBlock> _transformAndColors_ssbo;

	std::shared_ptr<VertexBufferBlock> _vertexBuffer;
	std::shared_ptr<IndexBufferBlock> _indexBuffer;
	std::shared_ptr<IndirectBufferBlock> _indirectBuffer;

	std::vector<IndirectDrawCommand> _commands;

	IndirectDrawCommand _cubeCommandTemplate;
	IndirectDrawCommand _sphereCommandTemplate;
};