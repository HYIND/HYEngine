#pragma once

constexpr int Max_Color_Buffer_Count = 10;

#include "OpenGLRenderEngine/Base/Shader.h"

class CombinPass
{
public:
	CombinPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath,
		float SCR_WIDTH, float SCR_HEIGHT);
	void Draw(GLuint destFbo, const std::vector<GLuint>& colorBuffers);

private:
	float _width;
	float _height;

	Shader _shader;
};