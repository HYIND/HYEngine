#pragma once

#include "VulkanRenderEngine/Base/Shader.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"

class FirstPersonPass
{
public:
	FirstPersonPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	void Draw(RenderState& state);

private:
	Shader _shader;
};