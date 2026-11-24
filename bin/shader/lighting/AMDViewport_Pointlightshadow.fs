#version 460 core

#include "shader/dataDef/ShadowMaterialTextureDef.comp"

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
    ShadowMaterialData material = shadowMaterials[data[dataIndex].materialIndex];
    if (calculateOpacity(material, FragTextureCoords) < 0.02)
        discard;

    // 不再写 gl_FragDepth：深度用 cube 面透视投影的硬件深度，这样硬件斜率深度偏移（depthBias）才生效，
    // 也不会关掉 early-Z。采样端按面的朝向轴距离算参考深度（lightingHelper.comp 的 PointShadowFaceDepth）
}
