#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

#include "shader/dataDef/camerauboDef.comp"

layout(binding = 0, std430) buffer Transforms{
    mat4 models[];
};

void main()
{
    vec4 worldPos = models[gl_BaseInstance] * vec4(aPos, 1.0); 
    gl_Position = camera.projection * camera.view * worldPos;
}