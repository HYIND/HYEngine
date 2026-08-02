#pragma once

#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"

class FirstPersonPass
{
public:
	FirstPersonPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	void Draw(RenderState& state);

private:
	Shader _shader;
};