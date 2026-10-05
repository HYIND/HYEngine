#version 460 core

#include "shader/dataDef/MaterialTextureDef.comp"

struct LightProp{
    vec3 lightPos;
    float farPlane;
};

layout(binding = 6) buffer LightProps
{
	LightProp lightProp[];
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

layout (location = 0) in vec2 FragTextureCoords;
layout (location = 1) flat in uint viewIndex;
layout (location = 2) flat in uint dataIndex;
layout (location = 3) in vec3 WorldPos;

void main()
{
    MaterialData material = materials[data[dataIndex].materialIndex];
    if (calculateOpacity(material, FragTextureCoords) < 0.02)
        discard;
        
    float lightDistance = length(WorldPos - lightProp[viewIndex].lightPos);
    gl_FragDepth = lightDistance / lightProp[viewIndex].farPlane;
}