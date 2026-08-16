#version 460 core
#extension GL_AMD_vertex_shader_viewport_index : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

#include "shader/Helper/animationHelper.comp"

flat out int Index;

layout(std430, binding = 4) buffer ShadowMatrices
{
	mat4 shadowMatrices[];
};

uniform mat4 model;

void main()
{
    Index = gl_InstanceID;

    vec4 animatedPos = CalculateIfHasAnimationData(vec4(aPos, 1.0));
    vec4 worldPos = model * animatedPos;
        
    gl_ViewportIndex = Index;
    
    gl_Position = shadowMatrices[Index] * worldPos;
}