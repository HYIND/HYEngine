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
		float SCR_WIDTH, float SCR_HEIGHT);
	void Draw(GLuint fbo, GLuint colorBuffer, GLuint _bloomBlurMap,
		bool bloom_on, bool gamma_on, bool needFlipFinalFboY,
		float exposureValue, float gammaValue
	);

private:
	float _width;
	float _height;

	Shader _shader;
};