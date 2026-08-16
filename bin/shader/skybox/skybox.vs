#version 450 core
layout(location = 0) in vec3 position;		

out vec3 FragTextureCoords;

#include "shader/dataDef/camerauboDef.comp"

uniform mat4 rotview;

void main()
{   
	FragTextureCoords = position;

	vec4 pos = camera.projection * rotview * vec4(position, 1.0);
    gl_Position = pos.xyww;
} 
