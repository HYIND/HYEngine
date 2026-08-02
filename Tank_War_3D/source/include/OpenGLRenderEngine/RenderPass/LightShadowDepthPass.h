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
	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderState& state);
	virtual void FrameEnd(RenderState& state);

private:
	void CalculateShadowAtlas(RenderState& state);
	void processDirAndSpotLight(RenderState& state);
	void processPointLight(RenderState& state);

private:
	Shader _dirLightShadowDepthShader;
	Shader _pointLightShadowDepthShader;

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
};