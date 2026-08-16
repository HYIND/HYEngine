#version 330 core
layout(location = 0) in vec3 position;		
layout(location = 1) in vec3 normal;

#include "shader/dataDef/camerauboDef.comp"

uniform mat4 model;

void main()
{
    vec3 norm = normalize(normal);

    float extrudeAmount = 0.1f;

    vec4 oriPos = camera.projection * camera.view * model * vec4(position, 1.0f);
    vec4 extrudedPos = camera.projection * camera.view * model * vec4(position + extrudeAmount * norm , 1.0f);

    gl_Position = extrudedPos;
} 
