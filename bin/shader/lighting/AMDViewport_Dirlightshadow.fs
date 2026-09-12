#version 460 core

#include "shader/dataDef/MaterialTextureDef.comp"

layout (location = 0) flat in int Index;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) flat in uint dataIndex;

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
    MaterialData material = materials[data[dataIndex].materialIndex];
    float opacity = calculateOpacity(material, FragTextureCoords);
    if (opacity < 0.01)
        discard;
}