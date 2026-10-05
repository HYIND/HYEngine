#version 460 core
#extension GL_ARB_shader_viewport_layer_array : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

layout (location = 0) out vec2 FragTextureCoords;
layout (location = 1) flat out uint viewIndex;
layout (location = 2) flat out uint dataIndex;


layout(binding = 3) uniform Params
{
	uint commandsPerView;
};

layout(binding = 4) buffer ShadowMatrices
{
	mat4 shadowMatrices[];
};

struct TransMatIndex
{
    mat4 model;
    uint materialIndex;
};

layout(binding = 5) buffer Transforms
{
    TransMatIndex data[];
};

void main()
{
    dataIndex = gl_BaseInstance;
    viewIndex = gl_DrawID / commandsPerView;

    vec4 worldPos = data[dataIndex].model * vec4(aPos, 1.0);

    gl_ViewportIndex = int(viewIndex);
    FragTextureCoords = aTexCoords;
    gl_Position = shadowMatrices[viewIndex] * worldPos;
}