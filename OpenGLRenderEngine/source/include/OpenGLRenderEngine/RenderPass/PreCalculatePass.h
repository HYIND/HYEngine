#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "OpenGLRenderEngine/General/IndirectDrawManager.h"
#include "RenderPassBase.h"
#include <vector>

class PreCalculatePass :public RenderPassBase
{
public:
	PreCalculatePass();
	~PreCalculatePass();

	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderState& state);;
	virtual void FrameEnd(RenderState& state);;

private:
	std::vector<glm::mat4> _staticMesh_Transforms;
	std::shared_ptr<SSBO> _ssbo_StaticMesh_Transforms;
	GLuint _VAO;
	GLuint _indirectCommandBuffer;
};