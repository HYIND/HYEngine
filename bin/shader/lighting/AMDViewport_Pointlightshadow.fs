#version 460 core

#include "shader/dataDef/MaterialTextureDef.comp"

struct LightProp{
    vec3 lightPos;
    float farPlane;
};

layout(set = 0, binding = 6) buffer LightProps
{
	LightProp lightProp[];
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

layout (location = 0) flat in int Index;
layout (location = 1) in vec3 WorldPos;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) flat in uint dataIndex;

void main()
{
    MaterialData material = materials[data[dataIndex].materialIndex];
    float opacity = calculateOpacity(material, FragTextureCoords);
    if (opacity < 0.01)
        discard;

    float lightDistance = length(WorldPos - lightProp[Index].lightPos);
    gl_FragDepth = lightDistance / lightProp[Index].farPlane;
}