#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

#include "shader/Helper/animationHelper.comp"

uniform mat4 model;

void main()
{
    vec4 pos = CalculateIfHasAnimationData(vec4(aPos, 1.0));
    gl_Position = model * pos;
}
