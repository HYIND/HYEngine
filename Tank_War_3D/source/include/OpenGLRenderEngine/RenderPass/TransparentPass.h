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
	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderState& state);;

private:
	Shader _shader;
};