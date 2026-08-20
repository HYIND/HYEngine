#pragma once

#include "OpenGLRenderEngine/Base/AtlasMap.h"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"


class LightShadowDepthPass :public RenderPassBase
{
public:
	LightShadowDepthPass(
		const std::string& dirLightVertexShaderPath, const std::string& dirLightGeometryShaderPath, const std::string& dirLightFragmentShaderPath,
		const std::string& pointLightVertexShaderPath, const std::string& pointLightGeometryShaderPath, const std::string& pointLightFragmentShaderPath
	);
	virtual ~LightShadowDepthPass();
	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void FrameEnd(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);

private:
	void CalculateShadowAtlas(RenderState& state);
	void processDirAndSpotLight(RenderState& state);
	void processPointLight(RenderState& state);


	void RenderSceneLightShadowPassScene(
		RenderState& state, 
		Shader& shader_StaticMesh,
		Shader& shader_Skinned,
		GLsizei count,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& skinned
	);

	void RenderSceneLightShadowPassSceneInstance(
		RenderState& state, 
		Shader& shader_StaticMesh,
		Shader& shader_Skinned,
		GLsizei count,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes,
		std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& skinned
	);

private:
	Shader _dirLightShadowDepthStaticMeshShader;
	Shader _dirLightShadowDepthSkinnedShader;
	Shader _pointLightShadowDepthStaticMeshShader;
	Shader _pointLightShadowDepthSkinnedShader;

	bool useAMDViewportExt;

	GLuint _Fbo;

	bool _shouldUpdateTexture;

	int64_t _lastUpadteTime;
	int updateFrameDelta = 3;		//每3帧更新一次

	std::shared_ptr<AtlasMap> _atlas;

	struct
	{
		std::vector<std::shared_ptr<DirLightInfo>> dirLightInfos;
		std::vector<std::shared_ptr<PointLightInfo>> pointLightInfos;
		std::vector<std::shared_ptr<SpotLightInfo>> spotLightInfos;
	}_history;

	std::shared_ptr<SSBO> _ssbo_ShadowMatrices;
	std::shared_ptr<SSBO> _ssbo_LightProps;

	std::vector<glm::mat4> _staticMesh_Transforms;
};