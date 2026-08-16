#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"

class BloomPass
{
public:
	BloomPass(const std::string& ssaoVertexShaderPath, const std::string& ssaoFragmentShaderPath,
		uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	void Draw(GLuint brightColorBuffer);
	GLuint GetBloomBlurMap();
	void OnResize(uint32_t newWidth, uint32_t newHeight);

private:
	void init();

private:
	uint32_t SCR_WIDTH;
	uint32_t SCR_HEIGHT;

	Shader bloomBlurShader;
	GLuint pingpongFBO[2];
	GLuint pingpongColorBuffers[2];

	GLuint outPutTarget;
};