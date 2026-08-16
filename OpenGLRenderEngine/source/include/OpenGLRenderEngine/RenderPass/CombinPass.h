#pragma once

constexpr int Max_Color_Buffer_Count = 10;

#include "OpenGLRenderEngine/Base/Shader.h"

class CombinPass
{
public:
	CombinPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath,
		uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	void Draw(GLuint destFbo, const std::vector<GLuint>& colorBuffers);
	void OnResize(uint32_t newWidth, uint32_t newHeight);

private:
	uint32_t _width;
	uint32_t _height;

	Shader _shader;
};