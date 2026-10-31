#version 460 core

// 无材质粒子的 AlphaBlend 版本 —— Weighted Blended OIT 的第一段（累积）。
//
// attachment 0 (accum)  : Σ color*α*w , Σ α*w         混合 One / One
// attachment 1 (reveal) : Π (1-α)                      混合 Zero / (1 - src)
// 合成在 particle_oit_composite.fs 里做，因此这里不做任何排序。

#include "shader/dataDef/camerauboDef.comp"
#include "shader/effect/particle_common.comp"

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec3 FragWorldPos;
layout(location = 2) flat in vec4 FragColor;
layout(location = 3) flat in uint FragMask;
layout(location = 4) flat in uint FragMaterialIndex;
layout(location = 5) flat in uint FragFlags;

layout(location = 0) out vec4 OutAccum;
layout(location = 1) out vec4 OutReveal;

// McGuire & Bavoil 2013 的权重：近处/不透明的片元权重大，远处的快速衰减。
float OitWeight(float z, float alpha)
{
	float a = min(1.0, alpha * 10.0) + 0.01;
	return clamp(a * a * a * 1e8 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
}

void main()
{
	vec4 color = FragColor;

	if (!ApplyParticleMask(FragMask, FragUV, color))
		discard;

	float alpha = clamp(color.a, 0.0, 1.0);
	if (alpha <= 0.0)
		discard;

	// 用相机距离归一化到 [0,1) 当作 OIT 的深度项（与 TAA/RT 的深度尺度无关，只用来排序权重）。
	float depth = clamp(length(FragWorldPos - camera.position) / max(camera.farPlane, 1e-4), 0.0, 0.999);
	float weight = OitWeight(depth, alpha);

	OutAccum = vec4(color.rgb * alpha * weight, alpha * weight);
	OutReveal = vec4(alpha);
}
