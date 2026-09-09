#version 460 core

layout (location = 0) in vec3 position;		

layout (location = 0) flat out uint index;

#include "shader/dataDef/camerauboDef.comp"

struct Param
{
	mat4 model;
	vec3 color;
};

layout (binding = 0) buffer TransformAndColors
{
	Param params[];
};

void main()
{
	index = gl_BaseInstance;
	gl_Position = camera.projView * params[gl_BaseInstance].model * vec4(position, 1.0f);
} 
