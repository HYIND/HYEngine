#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SSRPass :public RenderPassBase
{
public:
	SSRPass(
		const std::string& ssrComputerShaderPath,
		const std::string& blurComputerShaderPath
	);

	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:

	struct FrameRenderData
	{
		glm::ivec2 drawSize;
		glm::ivec2 scrSize;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;

		std::shared_ptr<Texture2D> colorMap;
		std::shared_ptr<Texture2D> depthMap;
	};

	bool DrawSSR(FrameRenderData& data, RenderState& state);
	bool DrawBlur(FrameRenderData& data, RenderState& state);
	//bool DrawOverlay(RenderState& state);

private:
	Shader _ssrShader;
	Shader _blurShader;
};