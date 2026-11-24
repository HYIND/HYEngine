#version 460
#extension GL_EXT_ray_tracing : enable

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
    float coneLodBase;      // Ray Cone：0.5·log2(UV 面积 / 世界面积) - log2|n_geo · d|（见 ClosestHit.rchit）
};

layout(location = 0) rayPayloadInEXT HitPayLoad payload;

void main() {
    payload.isHit = false;
}