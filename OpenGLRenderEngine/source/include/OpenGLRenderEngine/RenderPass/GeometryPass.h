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
		const std::string& staticMeshFragmentShaderPath,
		const std::string& skinnedMeshvertexShaderPath,
		const std::string& skinnedFragmentShaderPath
	);
	~GeometryPass();

	void BindTexToFbo(
		std::shared_ptr<Texture2D>& gPosition,
		std::shared_ptr<Texture2D>& gNormal,
		std::shared_ptr<Texture2D>& gAlbedoOpacity,
		std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
		std::shared_ptr<Texture2D>& gMotionVectorMap,
		std::shared_ptr<Texture2D>& tempDepthStencilMap
	);

	virtual void EarlyExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);;

	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameEnd(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void SetupIndirecDrawMaterial(RenderState& state);
	bool SetupStaticBufferData(
		Shader& shader,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& items,
		OpenGLRenderObjectData::RenderIndex& renderIndex,
		std::vector<IndirectDrawCommand>& oneSideCommands,
		std::vector<IndirectDrawCommand>& twoSideCommands
	);
	void RenderSceneGeometryPassStatic(RenderState& state);
	void RenderSceneGeometryPassSkinned(RenderState& state);

private:
	Shader _staticShader;
	Shader _skinnedShader;
	GLuint _fbo;
};