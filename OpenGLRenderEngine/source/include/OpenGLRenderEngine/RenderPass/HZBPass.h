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

class HZBPass :public RenderPassBase
{
public:
	HZBPass(
		const std::string& sceneVertexShaderPath,
		const std::string& scenefragmentShaderPath,
		const std::string& HZBComputerShaderPath,
		const std::string& occlusionCullingComputerShaderPath
	);

	~HZBPass();

	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderState& state);
	virtual void FrameEnd(RenderState& state);

	uint32_t GetMaxLevel();

private:
	void DrawDepthMap(std::shared_ptr<Texture2D>& depthMap, RenderState& state);
	void DrawHZB(std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, RenderState& state);
	void GetOcclusionCulling(std::shared_ptr<Texture2D>& HZBMap, RenderState& state);

private:
	Shader _depthShader;
	Shader _HZBShader;
	Shader _occlusionCullShader;
	GLuint _Fbo;
	uint32_t _maxLevel = 7;

	std::vector<bool> _frustumCullResult;
	std::vector<uint32_t> _frustumObjectIndex;
	std::vector<AABB> _frustumObjectMeshaabbs;
	std::vector<int> _frustumOcclusionCullResult;

	std::vector<IndirectDrawCommand> _commands;
	std::vector<glm::mat4> _frustumObjectTransforms;
};