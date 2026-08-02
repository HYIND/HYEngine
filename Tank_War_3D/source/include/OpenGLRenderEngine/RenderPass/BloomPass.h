#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Camera.h"

class BloomPass
{
public:
	BloomPass(const std::string& ssaoVertexShaderPath, const std::string& ssaoFragmentShaderPath,
		float SCR_WIDTH, float SCR_HEIGHT);
	void Draw(GLuint brightColorBuffer);
	GLuint GetBloomBlurMap();

private:
	void init();

private:
	float SCR_WIDTH;
	float SCR_HEIGHT;

	Shader bloomBlurShader;
	GLuint pingpongFBO[2];
	GLuint pingpongColorBuffers[2];

	GLuint outPutTarget;
};