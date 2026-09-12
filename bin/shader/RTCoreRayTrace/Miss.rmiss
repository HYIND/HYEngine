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
};

layout(location = 0) rayPayloadInEXT HitPayLoad payload;

void main() {
    payload.isHit = false;
}