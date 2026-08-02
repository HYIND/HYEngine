#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class GeometryPassPass :public RenderPassBase
{
public:
	GeometryPassPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);

	void BindTexToFbo(
		std::shared_ptr<Texture2D>& gPosition,
		std::shared_ptr<Texture2D>& gNormal,
		std::shared_ptr<Texture2D>& gAlbedoOpacity,
		std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
		std::shared_ptr<Texture2D>& gMotionVectorMap,
		std::shared_ptr<Texture2D>& tempDepthStencilMap
	);

	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	Shader _shader;
	GLuint _fbo;
};