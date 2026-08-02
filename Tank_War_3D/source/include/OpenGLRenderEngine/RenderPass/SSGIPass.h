#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SSGIPass :public RenderPassBase
{
public:
	SSGIPass(
		const std::string& ssgiComputerShaderPath,
		const std::string& spatialDenoisingComputerShaderPath,
		const std::string& temporalDenoisingComputerShaderPath
	);

	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	void SetEnable(bool enable) const;

private:
	struct FrameRenderData
	{
		glm::ivec2 scrSize;
		glm::ivec2 drawSize;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> historyColorTexture;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> gMotionVector;
		std::shared_ptr<Texture2D> ssaoTexture;

		std::shared_ptr<Texture2D> colorMap;
		std::shared_ptr<Texture2D> depthMap;
	};

	bool DrawSSGI(FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(FrameRenderData& data, RenderState& state);
	bool DrawTemporalDenoising(FrameRenderData& data, RenderState& state);

private:
	Shader _ssgiShader;
	Shader _spatialDenoisingShader;
	Shader _temporalDenoisingShader;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;
};