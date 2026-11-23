#version 460
#extension GL_EXT_ray_tracing : enable

// 阴影光线的 miss：未被遮挡。payload 是 4 字节的 uint（发射前置 1 = 被挡），
// 与 shader/RTCoreRayTrace/RayTraceGeneral.comp 的 TraceShadowRay / shadowPayload 对应。
// 挂在 SBT 的 1 号 miss（RayTracingPipelineConfig::extraMissPaths 的第一项）。

layout(location = 1) rayPayloadInEXT uint shadowPayload;

void main() {
    shadowPayload = 0u;
}
