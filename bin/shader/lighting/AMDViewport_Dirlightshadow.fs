#version 460 core

#include "shader/dataDef/ShadowMaterialTextureDef.comp"

layout (location = 0) in vec2 FragTextureCoords;
layout (location = 1) flat in uint viewIndex;
layout (location = 2) flat in uint dataIndex;

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
    ShadowMaterialData material = shadowMaterials[data[dataIndex].materialIndex];
    if (calculateOpacity(material, FragTextureCoords) < 0.02)
        discard;
}