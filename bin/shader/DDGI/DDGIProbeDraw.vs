#version 460 core

// DDGI 探针可视化：每个槽位一个实例化球体。
// 被过滤掉的实例把位置放到裁剪空间外，由光栅化直接丢掉。

layout (location = 0) in vec3 position;      // 半径 0.5 的单位球

layout (location = 0) out vec3 outNormal;
layout (location = 1) flat out uint outSlot;
layout (location = 2) flat out uint outVolume;
layout (location = 3) out vec3 outWorldPos;

#include "shader/dataDef/camerauboDef.comp"

#define DDGI_UBO_BINDING 0
#define DDGI_VOLUME_BINDING 1
#define DDGI_PROBE_INFO_BINDING 2
#include "shader/DDGI/DDGICore.comp"
#include "shader/DDGI/DDGIUniforms.comp"

layout(set = 0, binding = 5) uniform sampler2D probeDataTex;

void main()
{
    uint slot = uint(gl_InstanceIndex);
    uint v = ddgiFindVolume(slot);
    outSlot = slot;
    outVolume = v;
    outNormal = normalize(position);
    outWorldPos = vec3(0.0);

    bool visible = v != DDGI_INVALID;
    if (visible && ddgi.debug.w != DDGI_INVALID && v != ddgi.debug.w)
        visible = false;

    ivec3 local = visible ? ddgiLocalCoord(slot - ddgiVolumeBase(v), ddgiVolumeCounts(v)) : ivec3(0);
    // 被覆盖的探针不参与采样，只在「状态」模式下显示
    if (visible && ddgi.debug.z != 1u && ddgiProbeCovered(v, local))
        visible = false;

    if (!visible)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec4 data = texelFetch(probeDataTex, ddgiTileOrigin(slot, 1u), 0);
    vec3 center = ddgiCellCenter(v, local) + (data.w >= 1.0 ? data.xyz : ddgiProbeManualOffset(slot));

    // drawParams.x 是相对最小间距的半径，position 是半径 0.5 的球
    float radius = ddgi.drawParams.x * ddgiMinSpacing(v);
    vec3 world = center + position * (2.0 * radius);
    outWorldPos = world;
    gl_Position = camera.projView * vec4(world, 1.0);
}
