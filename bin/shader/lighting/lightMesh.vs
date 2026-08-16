#version 450 core
layout(location = 0) in vec3 position;		

#include "shader/dataDef/camerauboDef.comp"

uniform mat4 model;

void main()
{
	gl_Position = camera.projection * camera.view * model * vec4(position, 1.0f);
} 
