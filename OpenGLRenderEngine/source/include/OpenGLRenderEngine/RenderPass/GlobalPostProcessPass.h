#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/General/RenderItem.h"

class GlobalPostProcessPass
{
public:
	GlobalPostProcessPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath,
		uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	void Draw(GLuint fbo, GLuint colorBuffer, GLuint _bloomBlurMap,
		bool bloom_on, bool gamma_on, bool needFlipFinalFboY,
		float exposureValue, float gammaValue
	);
	void OnResize(uint32_t newWidth, uint32_t newHeight);

private:
	uint32_t _width;
	uint32_t _height;

	Shader _shader;
};