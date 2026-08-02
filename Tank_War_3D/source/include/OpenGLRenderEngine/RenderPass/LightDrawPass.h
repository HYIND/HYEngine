#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "./RenderPassBase.h"

class LightDrawPass :public RenderPassBase
{
public:
	LightDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~LightDrawPass() = default;
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	Shader _shader;
};