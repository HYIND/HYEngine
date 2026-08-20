#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SkyBoxPass :public RenderPassBase
{
public:
	SkyBoxPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~SkyBoxPass();
	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	Shader _shader;

	GLuint _skyboxVAO;
	GLuint _skyboxVBO;
};