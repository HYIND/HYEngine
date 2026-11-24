#version 460
#extension GL_EXT_ray_tracing : enable

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
    uint skinned;               // 1 = 蒙皮网格，顶点要去蒙皮顶点缓冲里取
    uint skinnedVertexOffset;   // 该网格在蒙皮顶点缓冲里的起始顶点
    mat4 prevModel;             // 上一帧的模型矩阵（第一次出现时等于 model）
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
    vec3 prevPosition;      // 命中点上一帧的世界位置（物体运动；反射的时域累积用）。没有运动信息时等于 position
    float coneLodBase;      // Ray Cone：0.5·log2(UV 面积 / 世界面积) - log2|n_geo · d|，rgen 再加 log2(锥宽) 和贴图分辨率
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

// Ray Cone 里只和这个三角形有关的部分（与锥宽、贴图分辨率无关）：
//   0.5·log2(UV 面积 / 世界面积)：每单位世界长度对应多少 UV 长度
//   - log2|n_geo · d|：斜着打到表面时覆盖面被拉长
// UV 退化（整个三角形同一个 UV）时光栅化的导数是 0、读 mip 0，这里给一个很小的值让它同样落到 mip 0。
float rayConeLodBase(mat4 model, Vertex v0, Vertex v1, Vertex v2)
{
    mat3 model3 = mat3(model);
    vec3 worldCross = cross(model3 * (v1.position - v0.position), model3 * (v2.position - v0.position));
    float worldArea2 = length(worldCross);

    vec2 uv1 = v1.texCoords - v0.texCoords;
    vec2 uv2 = v2.texCoords - v0.texCoords;
    float uvArea2 = abs(uv1.x * uv2.y - uv2.x * uv1.y);

    if (worldArea2 <= 1e-12 || uvArea2 <= 1e-12)
        return -64.0;

    float cosTheta = abs(dot(worldCross / worldArea2, normalize(gl_WorldRayDirectionEXT)));
    return 0.5 * log2(uvArea2 / worldArea2) - log2(max(cosTheta, 1e-2));
}

void main() 
{

    uint instanceIndex = gl_InstanceCustomIndexEXT;
    InstanceInfo instanceInfo = infos[instanceIndex];

    // 拿到图元索引和三个顶点索引
    uint primitiveIndex = gl_PrimitiveID;
    uint i0 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 0];
    uint i1 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 1];
    uint i2 = indices[instanceInfo.indexOffset + primitiveIndex * 3 + 2];

    Vertex v0;
    Vertex v1;
    Vertex v2;

    if (instanceInfo.skinned != 0u) {
        // 蒙皮网格：顶点在本帧的蒙皮顶点缓冲里，索引是网格内局部索引，起始顶点由 skinnedVertexOffset 给
        uint base = instanceInfo.skinnedVertexOffset;
        v0 = skinnedVertices[base + i0];
        v1 = skinnedVertices[base + i1];
        v2 = skinnedVertices[base + i2];
    } else {
        v0 = vertices[instanceInfo.vertexOffset + i0];
        v1 = vertices[instanceInfo.vertexOffset + i1];
        v2 = vertices[instanceInfo.vertexOffset + i2];
    }

    // 重心坐标
    vec3 bary = vec3(1.0 - attrib.x - attrib.y, attrib.x, attrib.y);

    vec3 localPos = lerpVec3(v0.position, v1.position, v2.position, bary);
    vec3 worldPos = vec3(instanceInfo.model * vec4(localPos, 1.0));
    // 上一帧位置：同一个局部点用上一帧的模型矩阵变换。蒙皮网格只算整体（模型矩阵）的运动，不含骨骼形变
    vec3 prevWorldPos = vec3(instanceInfo.prevModel * vec4(localPos, 1.0));

    // 插值法线、切线、副切线
    vec3 localTangent   = normalize(lerpVec3(v0.tangent,   v1.tangent,   v2.tangent,   bary));
    vec3 localBitangent = normalize(lerpVec3(v0.bitangent, v1.bitangent, v2.bitangent, bary));
    vec3 localNormal    = normalize(lerpVec3(v0.normal,    v1.normal,    v2.normal,    bary));

    // 用逆转置矩阵变换法线到世界空间
    payload.position = worldPos;  // 世界坐标位置
    payload.tangent   = normalize(instanceInfo.invTransModel * localTangent);
    payload.bitangent = normalize(instanceInfo.invTransModel * localBitangent);
    payload.normal    = normalize(instanceInfo.invTransModel * localNormal);
    payload.texCoords = lerpVec2(v0.texCoords, v1.texCoords, v2.texCoords, bary);// 插值 UV
    payload.t = gl_HitTEXT;     // 命中距离
    payload.materialIndex = instanceInfo.materialIndex;    // 材质索引
    payload.isHit = true;
    payload.prevPosition = prevWorldPos;
    payload.coneLodBase = rayConeLodBase(instanceInfo.model, v0, v1, v2);
}
