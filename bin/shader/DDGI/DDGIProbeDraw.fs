#version 460 core

// DDGI 探针可视化着色：
//   0 辐照度：球面上每个点显示该方向的辐照度（图集里存的 E/π）
//   1 状态：绿 = 正常，蓝 = 尚未更新，红 = 埋在几何里（背面命中多），黄 = 已自动偏移，
//          青 = 锁定（手动偏移），深红 = 禁用，灰 = 被更高优先级体积覆盖
//   2 距离：该方向的平均命中距离（近 = 红，远 = 蓝），按该体积的深度截断归一化
// 选中的探针加白色描边。

layout (location = 0) in vec3 inNormal;
layout (location = 1) flat in uint inSlot;
layout (location = 2) flat in uint inVolume;
layout (location = 3) in vec3 inWorldPos;

layout (location = 0) out vec4 FragColor;

#include "shader/dataDef/camerauboDef.comp"

#define DDGI_UBO_BINDING 0
#define DDGI_VOLUME_BINDING 1
#define DDGI_PROBE_INFO_BINDING 2
#include "shader/DDGI/DDGICore.comp"
#include "shader/DDGI/DDGIUniforms.comp"

layout(set = 0, binding = 3) uniform sampler2D irradianceTex;
layout(set = 0, binding = 4) uniform sampler2D depthTex;
layout(set = 0, binding = 5) uniform sampler2D probeDataTex;   // xyz 偏移，w = n + 背面比例

vec3 ddgiHeat(float t)
{
    t = clamp(t, 0.0, 1.0);
    return clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0);
}

void main()
{
    vec3 n = normalize(inNormal);
    uint flags = ddgiProbeFlags(inSlot);
    vec4 data = texelFetch(probeDataTex, ddgiTileOrigin(inSlot, 1u), 0);
    bool valid = data.w >= 1.0;

    vec3 color;
    uint mode = ddgi.debug.z;
    if (mode == 0u)
    {
        if (!valid)
            color = vec3(0.6, 0.0, 0.6);
        else
        {
            vec2 uv = ddgiProbeSampleUV(ddgiTileOrigin(inSlot, uint(DDGI_IRR_TILE)), n, uint(DDGI_IRR_INNER), vec2(textureSize(irradianceTex, 0)));
            color = textureLod(irradianceTex, uv, 0.0).rgb;
        }
    }
    else if (mode == 2u)
    {
        vec2 uv = ddgiProbeSampleUV(ddgiTileOrigin(inSlot, uint(DDGI_DEPTH_TILE)), n, uint(DDGI_DEPTH_INNER), vec2(textureSize(depthTex, 0)));
        float d = textureLod(depthTex, uv, 0.0).r;
        color = valid ? ddgiHeat(d / max(ddgiDepthClamp(inVolume), 1e-3)) : vec3(0.3);
    }
    else
    {
        float backface = fract(data.w);
        if ((flags & DDGI_PROBE_DISABLED) != 0u)        color = vec3(0.35, 0.0, 0.0);
        else if ((flags & DDGI_PROBE_INACTIVE) != 0u)   color = vec3(0.25);
        else if (!valid)                                color = vec3(0.1, 0.3, 1.0);
        else if ((flags & DDGI_PROBE_LOCKED) != 0u)     color = vec3(0.0, 0.9, 0.9);
        else if (backface > 0.55)                       color = vec3(1.0, 0.1, 0.1);
        else if (dot(data.xyz, data.xyz) > 1e-6)        color = vec3(1.0, 0.85, 0.1);
        else                                            color = vec3(0.1, 0.9, 0.2);
        // 简单的明暗，看得出球形
        color *= 0.55 + 0.45 * max(dot(n, normalize(camera.position - inWorldPos)), 0.0);
    }

    if ((flags & DDGI_PROBE_SELECTED) != 0u)
    {
        float rim = 1.0 - max(dot(n, normalize(camera.position - inWorldPos)), 0.0);
        if (rim > 0.55)
            color = vec3(4.0);
    }

    FragColor = vec4(color, 1.0);
}
