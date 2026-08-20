#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/Base/AtlasMap.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class LightingPass :public RenderPassBase
{
public:
	LightingPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	~LightingPass();
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);;

private:
	Shader _shader;
};