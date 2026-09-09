#version 460 core

layout (location = 0) flat in uint index;

layout (location = 0) out vec4 FragColor;

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
	FragColor = vec4(params[index].color, 1.0f);
}