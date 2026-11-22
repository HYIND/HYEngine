#version 460
#extension GL_EXT_ray_tracing : enable

// DDGI 专用 ClosestHit：同 shader/RTCoreRayTrace/ClosestHit.rchit，另用几何法线判断背面，
// 结果编码在 payload.t 的符号里（负 = 背面，|t| 为命中距离）。
// 插值法线在轮廓附近会偏到另一侧，不能用来判断背面；几何法线的绕序因模型而异，先按插值法线翻到外侧。

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec2 texCoords;
    vec3 tangent;
    vec3 bitangent;
};

struct InstanceInfo {
    mat4 model;
    mat4 invModel;
    mat3 invTransModel;
    uint materialIndex;
    uint indexOffset;
    uint vertexOffset;
    uint skinned;
    uint skinnedVertexOffset;
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

layout(binding = 4) readonly buffer SkinnedVertexBuffer {
    Vertex skinnedVertices[];
};

vec3 lerpVec3(vec3 v0, vec3 v1, vec3 v2, vec3 bary)
{
    return v0 * bary.x + v1 * bary.y + v2 * bary.z;
}

vec2 lerpVec2(vec2 v0, vec2 v1, vec2 v2, vec3 bary)
{
    return v0 * bary.x + v1 * bary.y + v2 * bary.z;
}

void main()
{
    uint instanceIndex = gl_InstanceCustomIndexEXT;
    InstanceInfo instanceInfo = infos[instanceIndex];

    uint primitiveIndex = gl_PrimitiveID;
    uint i0 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 0];
    uint i1 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 1];
    uint i2 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 2];

    Vertex v0;
    Vertex v1;
    Vertex v2;

    if (instanceInfo.skinned != 0u) {
        uint base = instanceInfo.skinnedVertexOffset;
        v0 = skinnedVertices[base + i0];
        v1 = skinnedVertices[base + i1];
        v2 = skinnedVertices[base + i2];
    } else {
        v0 = vertices[instanceInfo.vertexOffset + i0];
        v1 = vertices[instanceInfo.vertexOffset + i1];
        v2 = vertices[instanceInfo.vertexOffset + i2];
    }

    vec3 bary = vec3(1.0 - attrib.x - attrib.y, attrib.x, attrib.y);

    vec3 worldPos = vec3(instanceInfo.model * vec4(lerpVec3(v0.position, v1.position, v2.position, bary), 1.0));

    vec3 localTangent   = normalize(lerpVec3(v0.tangent,   v1.tangent,   v2.tangent,   bary));
    vec3 localBitangent = normalize(lerpVec3(v0.bitangent, v1.bitangent, v2.bitangent, bary));
    vec3 localNormal    = normalize(lerpVec3(v0.normal,    v1.normal,    v2.normal,    bary));

    vec3 shadingNormal = normalize(instanceInfo.invTransModel * localNormal);

    // 几何法线（世界空间），用插值法线确定外侧朝向
    vec3 localGeomNormal = cross(v1.position - v0.position, v2.position - v0.position);
    vec3 geomNormal = instanceInfo.invTransModel * localGeomNormal;
    float geomLen = length(geomNormal);
    geomNormal = geomLen > 1e-12 ? geomNormal / geomLen : shadingNormal;
    if (dot(geomNormal, shadingNormal) < 0.0)
        geomNormal = -geomNormal;

    bool backFace = dot(geomNormal, gl_WorldRayDirectionEXT) > 0.0;

    payload.position  = worldPos;
    payload.tangent   = normalize(instanceInfo.invTransModel * localTangent);
    payload.bitangent = normalize(instanceInfo.invTransModel * localBitangent);
    payload.normal    = shadingNormal;
    payload.texCoords = lerpVec2(v0.texCoords, v1.texCoords, v2.texCoords, bary);
    payload.t = backFace ? -gl_HitTEXT : gl_HitTEXT;
    payload.materialIndex = instanceInfo.materialIndex;
    payload.isHit = true;
}
