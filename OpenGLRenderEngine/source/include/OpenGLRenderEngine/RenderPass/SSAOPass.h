#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class SSAOPass :public RenderPassBase
{
public:
	SSAOPass(const std::string& ssaoVertexShaderPath, const std::string& ssaoFragmentShaderPath,
		const std::string& ssaoBlurVertexShaderPath, const std::string& ssaoBlurFragmentShaderPath
	);
	virtual ~SSAOPass();
	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	void BindToFbo(std::shared_ptr<Texture2D>& ssaoColorBuffer, std::shared_ptr<Texture2D>& ssaoBlurColorBuffer);

private:
	Shader ssaoShader;
	Shader ssaoBlurShader;

	GLuint ssaoFBO;
	GLuint ssaoBlurFBO;

	std::unique_ptr<Texture2D> noiseTexture;

	std::vector<glm::vec3> ssaoKernel;
};