#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class TransparentPass :public RenderPassBase
{
public:
	TransparentPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~TransparentPass() = default;

	virtual void EarlyExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);;

	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void SetupIndirecDrawMaterial(RenderState& state);

private:
	Shader _shader;
};