#version 460 core
#extension GL_ARB_shader_viewport_layer_array : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

layout (location = 0)  flat out int Index;
layout (location = 2) out vec2 FragTextureCoords;
layout (location = 3) flat out uint dataIndex;

#include "shader/Helper/animationHelper.comp"

layout(set = 0, binding = 4) buffer ShadowMatrices
{
	mat4 shadowMatrices[];
};

struct TransMatIndex
{
    mat4 model;
    uint materialIndex;
};

layout(set = 0, binding = 5) buffer Transforms
{
    TransMatIndex data[];
};

void main()
{
    // int InstanceID = gl_InstanceIndex - gl_BaseInstance;

    // vec4 animatedPos = CalculateIfHasAnimationData(vec4(aPos, 1.0));
    // vec4 worldPos = model * animatedPos;     
    
    // Index = InstanceID;
    // gl_ViewportIndex = Index;
    // gl_Position = shadowMatrices[Index] * worldPos;
}