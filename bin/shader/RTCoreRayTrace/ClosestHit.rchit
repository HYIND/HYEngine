#version 460
#extension GL_EXT_ray_tracing : enable

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec2 texCoords;
    vec3 tangent;
    vec3 bitangent;
    int padding;
    int m_BoneIDs[8];
    int m_Weights[8];
};

struct InstanceInfo {
    mat4 model;
    mat4 invModel;
    mat3 invTransModel;
    uint materialIndex;
    uint primitiveOffset;
};

struct HitPayLoad {
    vec3 position;
    vec3 tangent;
    vec3 bitangent;
    vec3 normal;
    vec2 texCoords;
    float t;
    uint materialIndex;
    bool isHit;
};

layout(location = 0) rayPayloadInEXT HitPayLoad payload;
hitAttributeEXT vec2 attrib;

layout(binding = 1) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(binding = 2) readonly buffer IndexBuffer {
    uint indices[];
};

layout(binding = 3) readonly buffer InstanceInfoBuffer {
    InstanceInfo infos[];
};

vec3 lerpVec3(vec3 v0, vec3 v1, vec3 v2, vec3 bary)
{
    return v0 * bary.x + v1 * bary.y + v2 * bary.z;
}

vec2 lerpVec2(vec2 v0, vec2 v1, vec2 v2, vec3 bary)
{
    return v0 * bary.x + v1 * bary.y + v2 * bary.z;
}

void main() {

    uint instanceIndex = gl_InstanceCustomIndexEXT;
    InstanceInfo instanceInfo = infos[instanceIndex];

    // 拿到图元索引和三个顶点索引
    uint primitiveIndex = gl_PrimitiveID;
    uint i0 = indices[instanceInfo.primitiveOffset + primitiveIndex * 3 + 0];
    uint i1 = indices[instanceInfo.primitiveOffset + primitiveIndex * 3 + 1];
    uint i2 = indices[instanceInfo.primitiveOffset + primitiveIndex * 3 + 2];

    Vertex v0 = vertices[i0];
    Vertex v1 = vertices[i1];
    Vertex v2 = vertices[i2];

    // 重心坐标
    vec3 bary = vec3(1.0 - attrib.x - attrib.y, attrib.x, attrib.y);

    vec3 worldPos = vec3(instanceInfo.model * vec4(lerpVec3(v0.position, v1.position, v2.position, bary), 1.0));

    // 插值法线、切线、副切线
    vec3 localNormal    = lerpVec3(v0.normal,    v1.normal,    v2.normal,    bary);
    vec3 localTangent   = lerpVec3(v0.tangent,   v1.tangent,   v2.tangent,   bary);
    vec3 localBitangent = lerpVec3(v0.bitangent, v1.bitangent, v2.bitangent, bary);

    // 用逆转置矩阵变换法线到世界空间
    payload.position = worldPos;  // 世界坐标位置
    payload.tangent   = normalize(instanceInfo.invTransModel * localTangent);
    payload.bitangent = normalize(instanceInfo.invTransModel * localBitangent);
    payload.normal    = normalize(instanceInfo.invTransModel * localNormal);
    payload.texCoords = lerpVec2(v0.texCoords, v1.texCoords, v2.texCoords, bary);// 插值 UV
    payload.t = gl_HitTEXT;     // 命中距离
    payload.materialIndex = instanceInfo.materialIndex;    // 材质索引
    payload.isHit = true;
}