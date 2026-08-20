#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class AutoExposurePass :public RenderPassBase
{
public:
	AutoExposurePass(const std::string& histogramComputerShaderPath);
	virtual ~AutoExposurePass() = default;
	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	Shader _histogramShader;
};