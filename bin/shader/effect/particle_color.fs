#version 460 core

// 无材质粒子：颜色直接用粒子的 tint/alpha，圆形/软边在片元里从方片裁出来。

#include "shader/effect/particle_common.comp"

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec3 FragWorldPos;
layout(location = 2) flat in vec4 FragColor;
layout(location = 3) flat in uint FragMask;
layout(location = 4) flat in uint FragMaterialIndex;
layout(location = 5) flat in uint FragFlags;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 color = FragColor;

	if (!ApplyParticleMask(FragMask, FragUV, color))
		discard;

	OutColor = color;
}
