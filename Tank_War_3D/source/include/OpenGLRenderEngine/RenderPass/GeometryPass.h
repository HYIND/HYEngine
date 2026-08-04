#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class GeometryPass :public RenderPassBase
{
public:
	GeometryPass(
		const std::string& staticMeshVertexShaderPath, 
		const std::string& skinnedMeshvertexShaderPath, 
		const std::string& fragmentShaderPath
	);

	void BindTexToFbo(
		std::shared_ptr<Texture2D>& gPosition,
		std::shared_ptr<Texture2D>& gNormal,
		std::shared_ptr<Texture2D>& gAlbedoOpacity,
		std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
		std::shared_ptr<Texture2D>& gMotionVectorMap,
		std::shared_ptr<Texture2D>& tempDepthStencilMap
	);

	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderState& state);
	virtual void FrameEnd(RenderState& state);

private:
	Shader _staticShader;
	Shader _skinnedShader;
	GLuint _fbo;

	std::vector<bool> _frustumCullResult;
	std::vector<size_t> _renderIndex;
};